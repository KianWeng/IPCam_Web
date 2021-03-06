rm -rf web/libffmpeg.wasm web/libffmpeg.js
export TOTAL_MEMORY=536870912
export EXPORTED_FUNCTIONS="[ \
    '_initDecoder', \
    '_uninitDecoder', \
    '_openDecoder', \
    '_closeDecoder', \
    '_openDecoderLiveH26x', \
    '_closeDecoderLiveH26x', \
    '_sendData', \
    '_decodeOnePacket', \
    '_seekTo', \
    '_main',
    '_malloc',
    '_free'
]"

echo "Running Emscripten..."
emcc web/decoder.c dist/lib/libavformat.a dist/lib/libavcodec.a dist/lib/libavutil.a dist/lib/libswscale.a \
    -O3 \
    -I "dist/include" \
    -s WASM=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
    -s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
    -s EXPORTED_RUNTIME_METHODS="['addFunction']" \
    -s RESERVED_FUNCTION_POINTERS=14 \
    -s FORCE_FILESYSTEM=1 \
    -o web/libffmpeg.js

echo "Finished Build"
