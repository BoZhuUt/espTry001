import subprocess
import os

Import("env")

def get_git_commit_count():
    try:
        # 获取当前分支的总提交次数（即你的 n）
        commit_count = subprocess.check_output(['git', 'rev-list', '--count', 'HEAD']).decode('utf-8').strip()
        
        # 拼接成 V1.0.n 格式
        return f"V1.0.{commit_count}"
    except Exception:
        # 如果还没建立 git 仓库或获取失败，保底显示
        return "V1.0.0"

# 获取动态版本号
git_version = get_git_commit_count()
print(f"*** Automatically detected Git Version: {git_version} ***")

# 注入到 C++ 编译环境的宏中
env.Append(CPPDEFINES=[
    ("AUTO_VERSION", env.StringifyMacro(git_version))
])