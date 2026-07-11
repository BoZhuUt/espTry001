import subprocess
import os

Import("env")

def get_git_version():
    try:
        # 1. 获取最新的 short commit hash (例如: a1b2c3d)
        commit_hash = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).decode('utf-8').strip()
        
        # 2. 获取当前的 branch 名称 (例如: main 或 master)
        # branch = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).decode('utf-8').strip()
        
        # 3. 组合成你想要的格式，比如 "V1.0-[Hash]"
        # 如果你本地有 git tag，也可以用 ['git', 'describe', '--tags', '--always']
        return f"V1.0-{commit_hash}"
    except Exception:
        # 如果不是 git 仓库或者获取失败，返回一个保底版本号
        return "V1.0-unknown"

# 获取动态版本号
git_version = get_git_version()
print(f"*** Automatically detected Git Version: {git_version} ***")

# 将版本号作为一个宏定义注入到 C++ 编译环境中
# 相当于在代码里动态加上了 #define AUTO_VERSION "V1.0-xxxxxxx"
env.Append(CPPDEFINES=[
    ("AUTO_VERSION", env.StringifyMacro(git_version))
])