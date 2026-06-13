#!/usr/bin/env python3
"""生成 compile_commands.json，供 clangd IntelliSense 使用。

clangd 的 fallback 命令将工作目录设为源文件所在目录，
因此 .clangd 中的 -I. 相对路径会解析错误。
compile_commands.json 通过 "directory" 字段将工作目录固定为项目根目录，
解决此问题。

此文件包含机器相关绝对路径，已在 .gitignore 中排除。
"""

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIRS = ["kernel", "arch/aarch64"]
CC = os.environ.get("CROSS_COMPILE", "aarch64-elf-") + "gcc"

FLAGS = (
    "-nostdlib -ffreestanding -I. -DBOARD_PI4 "
    "-mgeneral-regs-only -Wall -Wextra -O0 -g"
)

entries = []
for sd in SRC_DIRS:
    d = ROOT / sd
    if not d.is_dir():
        continue
    for f in sorted(d.iterdir()):
        if f.suffix in (".c", ".S"):
            entries.append({
                "directory": str(ROOT),
                "command": f"{CC} {FLAGS} -c {f.relative_to(ROOT)}",
                "file": str(f.relative_to(ROOT)),
            })

out = ROOT / "compile_commands.json"
with open(out, "w") as fh:
    json.dump(entries, fh, indent=2)
    fh.write("\n")

print(f"compile_commands.json: {len(entries)} entries")
