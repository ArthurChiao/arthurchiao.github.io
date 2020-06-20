" turn html elements to markdown elements
"
" Usage:
" 1. open html file with vim: `vim <file>`
" 2. execute this script: `: source html-to-markdown.vim`

" replace <p> with new line character
%s/<p>/\r/g

" delete </p>
%s/<\/p>//g

" convert header elements
%s/<h1>/# /g
%s/<\/h1>//g
%s/<h2>/## /g
%s/<\/h2>//g
%s/<h3>/### /g
%s/<\/h3>//g
%s/<h4>/#### /g
%s/<\/h4>//g
%s/<h5>/#### /g
%s/<\/h5>//g

" convert list
%s/<li>/1. /g
%s/<\/li>//g

" convert bold fonts
%s/<b>/**/g
%s/<\/b>/**/g
