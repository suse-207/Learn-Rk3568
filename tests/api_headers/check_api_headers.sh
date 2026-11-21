#!/usr/bin/env bash
# 公共 API 头卫生：com/api/** 不得出现绑定类型/协议名（ADR 0001 / C2 / C18）。
#
# 允许清单（例外必须登记，且注明收口阶段）：
#   runtime.h —— 仍暴露 IBindingRuntime / GetBindRuntime / RegisterBindRuntime，
#                属 ADR 的 C19，P5（Runtime 自装配绑定）收口后必须从清单移除。
set -uo pipefail

DIR="${1:?usage: check_api_headers.sh <com/api directory>}"
ALLOW=""
FORBIDDEN='BindHandle|BindSkeleton|IBinding|binding/|vsomeip'
fail=0

for f in "$DIR"/*.h; do
    base="$(basename "$f")"
    case " $ALLOW " in
        *" $base "*)
            echo "SKIP  $base（白名单，见 ADR C19 / P5）"
            continue
            ;;
    esac
    hits="$(grep -nE "$FORBIDDEN" "$f" | grep -vE '^[0-9]+:[[:space:]]*//' || true)"
    if [ -n "$hits" ]; then
        echo "FAIL  $base 出现绑定概念："
        echo "$hits" | sed 's/^/        /'
        fail=1
    else
        echo "PASS  $base"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "RESULT: FAILED（公共 API 泄漏绑定类型，见 docs/adr/0001-public-api-ownership.md C2）"
    exit 1
fi
echo "RESULT: PASSED"
