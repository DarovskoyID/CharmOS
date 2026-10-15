# Диск с закрытой областью (открытой, как её кладёт mkdisk) и свежий TPM.
build="$1"
bash "$(dirname "$0")/swtpm.sh" "$build"
echo "sysarea secret: only through the TPM" >"$build/sysarea-secret.txt"
"$build/host/tools/mkdisk/mkdisk" "$build/disk-sysarea.img" 64 --esp 40 --system 8 --root ext2 \
  --put "3:/secret.txt=$build/sysarea-secret.txt"
# До загрузки открытый текст на диске есть — иначе проверка ниже ничего не значит.
grep -qa "sysarea secret" "$build/disk-sysarea.img"
