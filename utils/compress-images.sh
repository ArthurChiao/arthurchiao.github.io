#!/bin/bash

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <image dir>"
    exit 0
fi

directory=$1

for file in "$directory"/*.png "$directory"/*.jpg; do
    if [ -f "$file" ]; then
		echo "Compressing $file ..."
        ffmpeg -i "$file" -compression_level 100 "${file}.mjpeg"
        mv "${file}.mjpeg" "$file"
    fi
done
