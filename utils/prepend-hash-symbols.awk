# add '#'s to header lines 
# e.g. "8. Some Title" -> "# 8. Some Title"
#
# Usage: awk -f prepend-hash-symbols.awk [input file] > [output file]

/^[0-9]+\. /                 { $0 = "# " $0; print $0; next}
/^[0-9]+\.[0-9]+ /           { $0 = "## " $0; print $0; next}
/^[0-9]+\.[0-9]+\.[0-9]+\. / { $0 = "### " $0; print $0; next}
                             { print }
