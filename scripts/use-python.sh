#!/bin/sh
# scripts/use-python.sh — micro-forge 项目 Python 环境网关(只项目内,不进全局)
#
# 用法: source scripts/use-python.sh
#
# 做四件事:
#   1. 没有 .venv 就建(python3 -m venv,只借系统 python 二进制引导,不往系统装包);
#   2. 往 .venv 装项目 Python 工具(目前:gcovr);
#   3. 激活 .venv(把 .venv/bin 放到 PATH 最前,设 VIRTUAL_ENV);
#   4. 定义 python3/python/pip/pip3 shell 函数:**只要当前激活的不是本项目 venv,
#      一律硬挡**——本机系统 python 的 pip 已被移除(禁用系统 python),任何绕过
#      本项目 venv 的调用都被挡下并提示正确入口,把调用者"赶到"正确方式。
#
# 只项目内:不写 ~/.zshrc、不改全局 PATH;只在 source 它的那个 shell 里生效。

# --- 定位项目根(从 $PWD 向上找 同时有 CMakeLists.txt 和 .git 的目录)---
_mf_root="$PWD"
while [ "$_mf_root" != "/" ]; do
    if [ -f "$_mf_root/CMakeLists.txt" ] && [ -d "$_mf_root/.git" ]; then
        break
    fi
    _mf_root="$(dirname "$_mf_root")"
done
if [ ! -f "$_mf_root/CMakeLists.txt" ] || [ ! -d "$_mf_root/.git" ]; then
    echo "use-python.sh: 不在 micro-forge 目录树下(找不到 CMakeLists.txt + .git)" >&2
    return 1 2>/dev/null || exit 1
fi

MF_PROJECT_VENV="$_mf_root/.venv"
export MF_PROJECT_VENV

# --- 1. 建 venv(只在不存在时建)---
if [ ! -x "$MF_PROJECT_VENV/bin/python3" ]; then
    echo "use-python.sh: 创建 venv → $MF_PROJECT_VENV"
    if ! python3 -m venv "$MF_PROJECT_VENV"; then
        echo "use-python.sh: venv 创建失败" >&2
        return 1 2>/dev/null || exit 1
    fi
fi

# --- 2. 装项目 Python 工具(缺则装;以后加新工具在这里扩展)---
if ! "$MF_PROJECT_VENV/bin/python3" -c "import gcovr" >/dev/null 2>&1; then
    echo "use-python.sh: 安装 gcovr"
    if ! "$MF_PROJECT_VENV/bin/pip" install --quiet --disable-pip-version-check gcovr; then
        echo "use-python.sh: ⚠ 安装 gcovr 失败,请手动: $MF_PROJECT_VENV/bin/pip install gcovr" >&2
    fi
fi

# --- 3. 激活 venv ---
export VIRTUAL_ENV="$MF_PROJECT_VENV"
case ":$PATH:" in
    *":$MF_PROJECT_VENV/bin:"*) ;;
    *) PATH="$MF_PROJECT_VENV/bin:$PATH"; export PATH ;;
esac

# --- 4. 硬挡函数:当前不是本项目 venv 一律挡,并提示正确入口 ---
python3() {
    if [ "$VIRTUAL_ENV" != "$MF_PROJECT_VENV" ]; then
        echo "⛔ python3 被挡:系统/其它 python 不可用。本项目请先: source scripts/use-python.sh" >&2
        return 1
    fi
    command python3 "$@"
}
python() { python3 "$@"; }   # python 复用 python3 的守卫
pip() {
    if [ "$VIRTUAL_ENV" != "$MF_PROJECT_VENV" ]; then
        echo "⛔ pip 被挡:系统 pip 不可用。本项目请先: source scripts/use-python.sh" >&2
        return 1
    fi
    command pip "$@"
}
pip3() { pip "$@"; }

unset _mf_root
echo "use-python.sh: ✓ 本项目 venv 已激活 → $(command python3 --version 2>&1) @ $MF_PROJECT_VENV"
