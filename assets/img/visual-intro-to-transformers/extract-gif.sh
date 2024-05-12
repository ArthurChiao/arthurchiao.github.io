#!/bin/bash

# if [[ $# -ne 1 ]]; then
#     echo "Usage: $0 <image dir>"
#     exit 0
# fi

input="input.mp4"

# ffmpeg -ss 7 -to 9 -i $input -filter_complex "fps=5" generative-meaning.gif -y
# ffmpeg -ss 11 -to 13 -i $input -filter_complex "fps=2.5" pre-trained-meaning.gif -y
# ffmpeg -ss 33 -to 35 -i $input -filter_complex "fps=5" transformer-detailed-1.gif -y
# ffmpeg -ss 41 -to 43 -i $input -filter_complex "fps=5" transformer-detailed-2.gif -y
# ffmpeg -ss 59 -to 61 -i $input -filter_complex "fps=3" dalle-pi.gif -y
# ffmpeg -ss 01:19 -to 01:22 -i $input -filter_complex "fps=1" machine-translation.gif -y
# ffmpeg -ss 01:36 -to 01:39 -i $input -filter_complex "fps=1" generative-transformer.gif -y
# ffmpeg -ss 02:16 -to 02:21 -i $input -filter_complex "fps=1" gpt2-output-1.gif -y
# ffmpeg -ss 02:44 -to 02:45 -i $input -filter_complex "fps=1" gpt3-output-1.gif -y
# ffmpeg -ss 03:13 -to 03:15 -i $input -filter_complex "fps=2" transformer-modules.gif -y
# ffmpeg -ss 03:40 -to 03:43 -i $input -filter_complex "fps=2" embedding-1.gif -y
# ffmpeg -ss 03:48 -to 03:54 -i $input -filter_complex "fps=2" embedding-2.gif -y
# ffmpeg -ss 03:57 -to 04:01 -i $input -filter_complex "fps=3" attention-1.gif -y
# ffmpeg -ss 04:26 -to 04:27 -i $input -filter_complex "fps=2" attention-2.gif -y
# ffmpeg -ss 04:32 -to 04:35 -i $input -filter_complex "fps=2" mlp-1.gif -y
# ffmpeg -ss 04:42 -to 04:46 -i $input -filter_complex "fps=1" mlp-2.gif -y
# ffmpeg -ss 04:49 -to 04:52 -i $input -filter_complex "fps=1.5" mlp-3.gif -y
# ffmpeg -ss 04:58 -to 05:00 -i $input -filter_complex "fps=1.5" matmul-1.gif -y
# ffmpeg -ss 05:22 -to 05:23 -i $input -filter_complex "fps=1" repeat-blocks.gif -y
# ffmpeg -ss 05:31 -to 05:32 -i $input -filter_complex "fps=1" last-vector.gif -y
# ffmpeg -ss 06:23 -to 06:28 -i $input -filter_complex "fps=2" system-prompt.gif -y
