#!/bin/bash

# if [[ $# -ne 1 ]]; then
#     echo "Usage: $0 <image dir>"
#     exit 0
# fi

input="input.mp4"

ffmpeg -ss 02:00 -to 02:01 -i $input -filter_complex "fps=2.0" slider-float.gif -y
