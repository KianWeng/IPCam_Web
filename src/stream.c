#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <iav_ioctl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/mman.h>

#ifndef AM_IOCTL
#define AM_IOCTL(_filp, _cmd, _arg)	\
		do { 						\
			if (ioctl(_filp, _cmd, _arg) < 0) {	\
				perror(#_cmd);		\
				return -1;			\
			}						\
		} while (0)
#endif

#define setbit(var, bit)   (var |= (1 << bit))
#define clrbit(var, bit)   (var &= (~(1 << bit)))
#define getbit(var, bit)   ((var >> bit) & 1)

#define MAX_ENCODE_STREAM_NUM	(IAV_STREAM_MAX_NUM_ALL)
// #define MAX_TRAN_SIZE 40000

static int fd_iav = -1;

static u8 *bsb_mem;
static u32 bsb_size;
static int stream_id_map;
static int client_id_map[MAX_ENCODE_STREAM_NUM];
static int first_connect_flag = 1;
// static int first_i_frame = 0;
extern int client_fds[8];
int write_video_run = 1;

static int start_encode(u32 stream_map)
{
	struct iav_queryinfo query_info;
	struct iav_stream_info *stream_info;
	int i;

	for (i = 0; i < MAX_ENCODE_STREAM_NUM; i++) {
		if (stream_map & (1 << i)) {
			memset(&query_info, 0, sizeof(query_info));
			query_info.qid = IAV_INFO_STREAM;
			stream_info = &query_info.arg.stream;
			stream_info->id = i;
			AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
			if (stream_info->state == IAV_STREAM_STATE_ENCODING) {
				stream_map &= ~(1 << i);
			}
		}
	}
	if (stream_map == 0) {
		printf("already in encoding, nothing to do \n");
		return 0;
	}

	AM_IOCTL(fd_iav, IAV_IOC_START_ENCODE, stream_map);

	printf("Start encoding for stream 0x%x successfully\n", stream_map);
	return 0;
}

static int stop_encode(u32 streamid)
{
	struct iav_queryinfo query_info;
	struct iav_stream_info *stream_info;
	u32 stop_streamid = streamid;
	u32 abort_streamid = streamid;
	int i;

	for (i = 0; i < MAX_ENCODE_STREAM_NUM; ++i) {
		if (stop_streamid & (1 << i)) {
			memset(&query_info, 0, sizeof(query_info));
			query_info.qid = IAV_INFO_STREAM;
			stream_info = &query_info.arg.stream;
			stream_info->id = i;
			AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
			if (stream_info->state != IAV_STREAM_STATE_ENCODING) {
				stop_streamid &= ~(1 << i);
			}
		}
	}
	if (stop_streamid) {
		printf("Stop encoding for stream 0x%x \n", stop_streamid);
		AM_IOCTL(fd_iav, IAV_IOC_STOP_ENCODE, stop_streamid);
	}

	for (i = 0; i < MAX_ENCODE_STREAM_NUM; ++i) {
		if (abort_streamid & (1 << i)) {
			memset(&query_info, 0, sizeof(query_info));
			query_info.qid = IAV_INFO_STREAM;
			stream_info = &query_info.arg.stream;
			stream_info->id = i;
			AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);
			if (stream_info->state != IAV_STREAM_STATE_STARTING &&
				stream_info->state != IAV_STREAM_STATE_STOPPING) {
				abort_streamid &= ~(1 << i);
			}
		}
	}
	if (abort_streamid) {
		printf("Abort encoding for stream 0x%x \n", abort_streamid);
		AM_IOCTL(fd_iav, IAV_IOC_ABORT_ENCODE, abort_streamid);
	}

	return 0;
}

static int abort_encode(u32 streamid)
{
	printf("abort encoding for stream 0x%x \n", streamid);
	AM_IOCTL(fd_iav, IAV_IOC_ABORT_ENCODE, streamid);

	return 0;
}

static int map_bsb(void)
{
	struct iav_querymem query_mem;
	struct iav_mem_part_info *part_info;

	query_mem.mid = IAV_MEM_PARTITION;
	part_info = &query_mem.arg.partition;
	part_info->pid = IAV_PART_BSB;
	if (ioctl(fd_iav, IAV_IOC_QUERY_MEMBLOCK, &query_mem) < 0) {
		perror("IAV_IOC_QUERY_MEMBLOCK");
		return -1;
	}

	if (part_info->mem.length == 0) {
		fprintf(stderr, "IAV_PART_BSB is not allocated.\n");
		return -1;
	}

	bsb_size = part_info->mem.length;
	bsb_mem = mmap(NULL, bsb_size * 2, PROT_READ, MAP_SHARED, fd_iav,
		part_info->mem.addr);
	if (bsb_mem == MAP_FAILED) {
		perror("mmap (%d) failed: %s\n");
		return -1;
	}

	printf("bsb_mem = %p, size = 0x%x\n", bsb_mem, bsb_size);

	return 0;
}

static int flush_frame_desc(void)
{
	struct iav_queryinfo query_info;
	struct iav_bsb_stats_info *bsb_stats;
	int rval = 0;

	memset(&query_info, 0, sizeof(query_info));
	query_info.qid = IAV_INFO_BSB_STATS;
	bsb_stats = &query_info.arg.bsb_stats;
	AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);

	if ((bsb_stats->frame_drop_cnt != 0) && (bsb_stats->bsb_mode == IAV_DUAL_BSB)) {
		printf("In IAV DUAL BSB mode, frame drop count[%d], frame lock queue will be flushed \n",
			bsb_stats->frame_drop_cnt);
		AM_IOCTL(fd_iav, IAV_IOC_FLUSH_FRAMEDESC, NULL);
	}

	return rval;
}

static int release_frame_desc(struct iav_framedesc *frame_desc)
{
	AM_IOCTL(fd_iav, IAV_IOC_RELEASE_FRAMEDESC, frame_desc);
	return 0;
}

static u8 is_new_frame(struct iav_framedesc *framedesc)
{
	u8 new_frame = 0;
	struct iav_stream_cfg streamcfg;
	int slice_per_info = IAV_ONE_BITS_INFO_PER_TILE;

	if (framedesc->stream_type == IAV_STREAM_TYPE_H265) {
		streamcfg.id = framedesc->id;
		streamcfg.cid = IAV_H265_CFG_SLICE;
		ioctl(fd_iav, IAV_IOC_GET_STREAM_CONFIG, &streamcfg);
		slice_per_info = streamcfg.arg.h265_slice.slices_per_info;
	} else {
		new_frame = 1;
		return new_frame;
	}

	switch (slice_per_info) {
	case IAV_ONE_BITS_INFO_PER_TILE:
		if ((framedesc->tile_id == 0) && (framedesc->slice_id == 0)) {
			new_frame = 1;
		}
		break;
	case IAV_ONE_BITS_INFO_PER_FRAME:
		if ((framedesc->tile_id == framedesc->tile_num - 1) &&
			(framedesc->slice_id == framedesc->slice_num - 1)) {
			new_frame = 1;
		}
		break;
	default:
		if ((framedesc->slice_id == slice_per_info - 1)) {
			new_frame = 1;
		}
		break;
	}

	return new_frame;
}

static u8 is_last_framedesc(struct iav_framedesc *framedesc)
{
	u8 is_lastdesc = 0;

	if (framedesc->stream_type == IAV_STREAM_TYPE_H265) {
		if ((framedesc->tile_id == framedesc->tile_num - 1) &&
			(framedesc->slice_id == framedesc->slice_num - 1)) {
			is_lastdesc = 1;
		}
	} else {
		is_lastdesc = 1;
	}

	return is_lastdesc;
}

static int write_video_file(struct iav_framedesc *framedesc, int new_frame)
{
	u32 pic_size = framedesc->size;
	int stream_id = framedesc->id;
	int i;
	/*char *data = NULL;
	//enum iav_stream_type stream_type = framedesc->stream_type;

	data=(char *)malloc(pic_size);
	if(!data){
		printf("Not enough memory\n");
		return -1;
	}

	memset(data, 0, pic_size);
	memcpy(data, bsb_mem + framedesc->data_addr_offset, pic_size);	*/
	printf("write frame %d, size %d, frame type %d.\n",framedesc->frame_num, pic_size, framedesc->pic_type);
	// if((framedesc->pic_type != 1) && (first_i_frame != 1)){
	// 	return 0;
	// } else {
	// 	first_i_frame = 1;
	// }
	for(i = 0; i < 8; i++){
		if(getbit(client_id_map[stream_id], i)){
			//将每帧数据封装成websocket包然后再发送
			// if (pic_size > MAX_TRAN_SIZE) {
			// 	do {
			// 		len = MAX_TRAN_SIZE;
			// 		if((pic_size - len) > 0){
			// 			response(client_fds[i], bsb_mem + framedesc->data_addr_offset + offset, len, 0x2);
			// 		} else {
			// 			response(client_fds[i], bsb_mem + framedesc->data_addr_offset + offset, pic_size, 0x2);
			// 		}
			// 		pic_size -= len;
			// 		offset += len;
			// 	} while ( pic_size > 0);
			// } else {
			response(client_fds[i], bsb_mem + framedesc->data_addr_offset, pic_size, 0x2);
			// }
			//if (write(client_fds[i], bsb_mem + framedesc->data_addr_offset, pic_size) < 0) {
			//	perror("Failed to write specify streams into client!\n");
			//}
		}
	}
	
	return 0;
}

static int write_stream(u64 *total_frames, u64 *total_bytes)
{
	struct iav_queryinfo query_info;
	struct iav_stream_info *stream_info;
	struct iav_querydesc query_desc;
	struct iav_framedesc *frame_desc;
	int new_frame;
	int last_desc;
	int i;

	for (i = 0; i < MAX_ENCODE_STREAM_NUM; ++i) {
		if(getbit(stream_id_map, i)){
			query_info.qid = IAV_INFO_STREAM;
			stream_info = &query_info.arg.stream;
			stream_info->id = i;
			AM_IOCTL(fd_iav, IAV_IOC_QUERY_INFO, &query_info);

			if (stream_info->state == IAV_STREAM_STATE_ENCODING) {
				continue;
			}else{
				//start encode stream
			}
		}
	}

	memset(&query_desc, 0, sizeof(query_desc));
	frame_desc = &query_desc.arg.frame;
	query_desc.qid = IAV_DESC_FRAME;
	frame_desc->id = -1;
	if (ioctl(fd_iav, IAV_IOC_QUERY_DESC, &query_desc) < 0) {
		if (errno != EAGAIN) {
			perror("IAV_IOC_QUERY_DESC");
		}
		return -1;
	}

	new_frame = is_new_frame(frame_desc);
	last_desc = is_last_framedesc(frame_desc);

	//check if it's a stream end null frame indicator
	if (frame_desc->stream_end) {
		goto write_stream_exit;
	}

	//write file if file is still opened
	if (write_video_file(frame_desc, new_frame) < 0) {
		fprintf(stderr, "write video file failed for stream %d, session id = %d \n",
			frame_desc->id, frame_desc->session_id);
		return -2;
	}

	release_frame_desc(frame_desc);

	//update global statistics
	if (total_frames && new_frame)
		*total_frames = (*total_frames) + 1;
	if (total_bytes)
		*total_bytes = (*total_bytes) + frame_desc->size;

write_stream_exit:
	return 0;
}

void *capture_encoded_video(void *arg)
{
	int rval;
	u64 total_frames;
	u64 total_bytes;
	total_frames = 0;
	total_bytes =  0;

	flush_frame_desc();
	while (write_video_run) {
		if(stream_id_map){
			if ((rval = write_stream(&total_frames, &total_bytes)) < 0) {
				if (rval == -1) {
					usleep(80 * 1000);
				} else {
					fprintf(stderr, "write_stream err code %d \n", rval);
					break;
				}
				//continue;
			}

		}
		//printf("send %ld frames, %ld bytes.\n", total_frames, total_bytes);
		usleep(10 * 1000);//等待30ms左右
	}

	printf("stop encoded stream capture\n");

	printf("total_frames = %lld\n", total_frames);
	printf("total_bytes = %lld\n", total_bytes);

	return ((void *)0);
}

int init_stream(){
    int ret = 0, err;
	pthread_t do_trans_stream_tid;

    if ((fd_iav = open("/dev/iav", O_RDWR, 0)) < 0) {
		perror("/dev/iav");
		ret = -1;
	}

    if (map_bsb() < 0) {
		fprintf(stderr, "map bsb failed\n");
		ret = -1;
	}

	err = pthread_create(&do_trans_stream_tid, NULL, capture_encoded_video, NULL);
	if (err != 0) {
		fprintf(stderr, "can't create capture encoded video thread: %s\n", strerror(err));
		ret = -2;
	}

	return ret;
}

int handle_client_msg(int client_id, char *msg){
	int  ret = 0, stream_id, i = 0;
	char *result = NULL;
	char cmd[4] = {};
	const char *d = ",";

	result = strtok(msg, d);
	while(result){
		//printf("i = %d: result is: %s\n",i, result);
		cmd[i] = atoi(result);
		//printf("cmd[%d] = 0x%x\n",i, cmd[i]);
		i = i + 1;
		result = strtok(NULL,d);
	}
	stream_id = cmd[3];

	if(client_fds[client_id] == 0){
		printf("[Handle Client Msg]client is not connect.\n");
		ret = -1;
		return ret;
	}

	if((cmd[0] == 0x5a) && (cmd[1] == 0xa5)){
		printf("[Handle Client Msg]get a correct cmd from client.\n");	
	}else{
		printf("[Handle Client Msg]invalid cmd.\n");
		ret = -2;
		return ret;
	}

	switch(cmd[2]){
		case 0x01:
			printf("[Handle Client Msg]receive start transport stream %d cmd from client.\n", stream_id);
			stream_id_map = setbit(stream_id_map, stream_id);
			client_id_map[stream_id] = setbit(client_id_map[stream_id], client_id);
			start_encode(stream_id_map);
			break;
		case 0x02:
			printf("[Handle Client Msg]receive stop transport stream %d cmd from client.\n", stream_id);
			stream_id_map = clrbit(stream_id_map, stream_id);
			client_id_map[stream_id] = clrbit(client_id_map[stream_id], client_id);
			stop_encode(0x1);
			break;
		default:
			printf("[Handle Client Msg]Unknow cmd 0x%x.\n",cmd);
			break;
	}

	return ret;
}