#!/bin/bash
# 启动本地聊天界面：把 cmake 3.28.6 放到 PATH 前面（系统 3.10.2 不支持 -S/-B），
# 并确保 /usr/local/bin 里的 cppcheck 2.13 可见。
cd "$(dirname "$0")" || exit 1
export PATH=/home/topeet/Desktop/cmake-3.28.6-linux-x86_64/bin:/usr/local/bin:$PATH
exec .venv/bin/streamlit run chat_app.py \
  --server.address 0.0.0.0 --server.port 8501 --server.headless true
