#!/bin/bash

# if [[ $# -ne 1 ]]; then
#     echo "Usage: $0 <image dir>"
#     exit 0
# fi

input="input.mp4"

ffmpeg -ss 05:20 -to 05:21 -i $input -filter_complex "fps=5" freq-scale-up.gif -y
