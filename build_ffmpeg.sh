echo "Beginning Build:"
rm -rf dist
mkdir -p dist
cd ../../ffmpeg
echo "emconfigure"
emconfigure ./configure --cc="emcc" --cxx="em++" --ar="emar" --ranlib="emranlib" --prefix=$(pwd)/../IPCam_Web/dist --enable-cross-compile --target-os=none \
        --arch=x86_32 --cpu=generic --enable-gpl --enable-version3 --disable-avdevice --disable-swresample --disable-postproc --disable-avfilter \
        --disable-programs --disable-logging --disable-everything --enable-avformat --enable-decoder=hevc --enable-decoder=h264 --enable-decoder=aac \
        --disable-ffplay --disable-ffprobe --disable-asm --disable-doc --disable-devices --disable-network --disable-hwaccels \
        --disable-parsers --disable-bsfs --disable-debug --enable-protocol=file --enable-demuxer=mov --enable-demuxer=flv --disable-indevs --disable-outdevs \
        --enable-parser=h264 --enable-demuxer=h264 --enable-muxer=h264 --enable-parser=hevc --enable-demuxer=hevc --enable-muxer=hevc
if [ -f "Makefile" ]; then
  echo "make clean"
  make clean
fi
echo "make"
make -j8
echo "make install"
make install
cd ../IPCam_Web
./build_wasm.sh
