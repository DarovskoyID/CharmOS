source "$(dirname "$0")/desktop.sh" "$1"
printf 'int main() {\n  return 42;\n}\n' >"$work/sample.cpp"
printf '\\section{CharmOS}\nE = mc^2\n' >"$work/formula.tex"
puts+=(--put "2:/sample.cpp=$work/sample.cpp" --put "2:/formula.tex=$work/formula.tex")
"$build/host/tools/mkdisk/mkdisk" "$build/disk-desktop.img" 96 --esp 40 --root ext2 "${puts[@]}"
