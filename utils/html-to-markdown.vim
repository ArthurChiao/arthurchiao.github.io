" turn html elements to markdown elements
"
" Usage:
" 1. open html file with vim: `vim <file>`
" 2. execute this script: `: source html-to-markdown.vim`

" replace <p> with new line character
" 
" '{-}': non-greedy search, match until first appearance,
" see https://stackoverflow.com/questions/3625596/vim-select-until-first-match/3625645
%s/<p.\{-\}>/\r/g

" delete </p>
%s/<\/p>//g

" convert header elements
%s/<h1.\{-\}>/# /g
%s/<\/h1>//g
%s/<h2.\{-\}>/## /g
%s/<\/h2>//g
%s/<h3.\{-\}>/### /g
%s/<\/h3>//g
%s/<h4.\{-\}>/#### /g
%s/<\/h4>//g
%s/<h5.\{-\}>/#### /g
%s/<\/h5>//g

" convert list
%s/<li.\{-\}>/1. /g
%s/<\/li>//g

" convert bold fonts
%s/<b>/**/g
%s/<\/b>/**/g

" convert code
%s/<code.\{-\}>/`/g
%s/<\/code>/`/g

" convert em (emphasize, usually Italic fonts)
%s/<em.\{-\}>/*/g
%s/<\/em>/*/g

" convert ol (ordered list) and ul (unordered list)
%s/<ol.\{-\}>//g
%s/<\/ol>//g
%s/<ul.\{-\}>//g
%s/<\/ul>//g

" delete trailing whitespaces
%s/\s\+$//g

" un-escape special characters
%s/&rsquo;/'/g
%s/&ldquo;/"/g
%s/&rdquo;/"/g
%s/&ndash;/-/g
%s/&mldr;/.../g

" must use escape character for the target '&'
%s/&amp;/\&/g

%s/&lt;/</g
%s/&gt;/>/g
