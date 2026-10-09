#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 收集构建矩阵：扫描 main/*/build_targets.txt，生成 GitHub Actions matrix JSON
# ---------------------------------------------------------------------------
# 用法：
#   ./scripts/gen_matrix.sh
#
# 输出（stdout）：
#   {"include":[{"project":"gateway","chip":"CH592","target":"gateway"},...]}
#
# build_targets.txt 格式（键值对）：
#   chip = CH592
#   chip = CH591
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$ROOT/main"

echo -n '{"include":['
first=1
for dir in "$MAIN"/*/; do
    project="$(basename "$dir")"
    f="$dir/build_targets.txt"
    [ -f "$f" ] || continue
    while IFS= read -r line; do
        # 跳过注释与空行
        line="${line%%#*}"
        # 去掉首尾空白
        line="$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        [ -z "$line" ] && continue
        # 解析 `chip = CHxxx`
        if [[ "$line" =~ ^[Cc][Hh][Ii][Pp][[:space:]]*=[[:space:]]*(.+)$ ]]; then
            chip="$(echo "${BASH_REMATCH[1]}" | sed -e 's/[[:space:]]*$//')"
            [ -z "$chip" ] && continue
            [ $first -eq 0 ] && echo -n ','
            first=0
            echo -n "{\"project\":\"$project\",\"chip\":\"$chip\",\"target\":\"$project\"}"
        fi
    done < "$f"
done
echo ']}'
