" replace tab with 4 whitespaces
%s/	/    /g

" remove all 'err' variables
%s/, err :=/ :=/g
%s/, err =/ =/g


" remove following snippet
" if err != nil {
"     return err
" }
:%g/\s\+if err != nil {\n\s\+return err\n\s\+}/d3

" simplify ctx argument
%s/ctx context.Context/ctx /g
