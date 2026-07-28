#!/bin/bash


set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检测系统信息
detect_system() {
    print_info "检测系统信息..."
    
    OS=$(uname -s)
    ARCH=$(uname -m)
    
    print_info "操作系统: $OS"
    print_info "架构: $ARCH"
    
    # 设置目标架构
    if [[ "$ARCH" == "aarch64" ]]; then
        TARGET_ARCH="arm64"
        print_info "目标架构: ARM64 (NEON)"
    elif [[ "$ARCH" == "armv7l" ]]; then
        TARGET_ARCH="arm"
        print_info "目标架构: ARM (NEON)"
    elif [[ "$ARCH" == "mips" ]]; then
        TARGET_ARCH="mips"
        print_info "目标架构: MIPS"
    else
        TARGET_ARCH="x86"
        print_info "目标架构: x86/x64"
    fi
}

# 检查依赖
check_dependencies() {
    print_info "检查构建依赖..."
    
    # 检查编译器
    if ! command -v gcc &> /dev/null; then
        print_error "GCC编译器未找到"
        exit 1
    fi
    
    if ! command -v g++ &> /dev/null; then
        print_error "G++编译器未找到"
        exit 1
    fi
    
    print_info "编译器版本:"
    gcc --version | head -n1
    g++ --version | head -n1
    
    # 检查make
    if ! command -v make &> /dev/null; then
        print_error "Make工具未找到"
        exit 1
    fi
    
    print_info "Make版本:"
    make --version | head -n1
}

# 清理构建目录
clean_build() {
    print_info "清理构建目录..."
    make clean
    rm -rf obj/
    print_info "构建目录已清理"
}

# 构建项目
build_project() {
    print_info "开始构建项目..."
    
    # 创建必要的目录
    mkdir -p obj/modules/audio_processing/aecm
    mkdir -p obj/modules/audio_processing/utility
    mkdir -p obj/common_audio/signal_processing
    mkdir -p obj/rtc_base
    mkdir -p obj/test
    
    # 构建静态库
    print_info "构建静态库..."
    make libaecm.a
    
    if [ -f "libaecm.a" ]; then
        print_info "静态库构建成功: libaecm.a"
    else
        print_error "静态库构建失败"
        exit 1
    fi
    
    # 构建测试程序
    print_info "构建测试程序..."
    make aecm_test
    
    if [ -f "aecm_test" ]; then
        print_info "测试程序构建成功: aecm_test"
    else
        print_error "测试程序构建失败"
        exit 1
    fi
}

# 运行测试
run_test() {
    print_info "运行测试程序..."
    
    if [ -f "aecm_test" ]; then
        ./aecm_test
        if [ $? -eq 0 ]; then
            print_info "测试程序运行成功"
        else
            print_error "测试程序运行失败"
            exit 1
        fi
    else
        print_error "测试程序未找到"
        exit 1
    fi
}

# 显示构建信息
show_build_info() {
    print_info "构建完成！"
    echo
    echo "生成的文件:"
    echo "  - libaecm.a (静态库)"
    echo "  - aecm_test (测试程序)"
    echo
    echo "使用方法:"
    echo "  - 将 libaecm.a 链接到您的项目中"
    echo "  - 包含头文件: src/modules/audio_processing/aecm/"
    echo "  - 运行测试: ./aecm_test"
    echo
    echo "支持的平台:"
    echo "  - x86/x64 (通用C实现)"
    echo "  - ARM/ARM64 (NEON优化)"
    echo "  - MIPS (MIPS优化)"
}

# 主函数
main() {
    print_info "开始构建独立AECM项目..."
    echo
    
    detect_system
    echo
    
    check_dependencies
    echo
    
    if [ "$1" == "clean" ]; then
        clean_build
        exit 0
    fi
    
    build_project
    echo
    
    if [ "$1" == "test" ]; then
        run_test
        echo
    fi
    
    show_build_info
}

# 显示帮助
show_help() {
    echo "独立AECM项目构建脚本"
    echo
    echo "用法: $0 [选项]"
    echo
    echo "选项:"
    echo "  clean    清理构建目录"
    echo "  test     构建并运行测试"
    echo "  help     显示此帮助信息"
    echo
    echo "示例:"
    echo "  $0          # 构建项目"
    echo "  $0 clean    # 清理构建目录"
    echo "  $0 test     # 构建并运行测试"
}

# 处理命令行参数
case "$1" in
    clean)
        main clean
        ;;
    test)
        main test
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        main
        ;;
esac
