#!/bin/bash
#
# GStreamer Media Framework - Installation Script
# This script installs all required dependencies and builds the project
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print functions
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [ "$EUID" -eq 0 ]; then
        print_warning "Running as root is not recommended for building"
    fi
}

# Detect Linux distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/debian_version ]; then
        echo "debian"
    elif [ -f /etc/redhat-release ]; then
        echo "rhel"
    else
        echo "unknown"
    fi
}

# Install dependencies for Debian/Ubuntu
install_debian() {
    print_info "Installing dependencies for Debian/Ubuntu..."
    
    sudo apt-get update
    
    # Build tools
    sudo apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        git
    
    # GStreamer core
    sudo apt-get install -y \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstreamer-plugins-good1.0-dev \
        libgstreamer-plugins-bad1.0-dev \
        libgstreamer-plugins-ugly1.0-dev
    
    # GStreamer plugins
    sudo apt-get install -y \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        gstreamer1.0-tools \
        gstreamer1.0-x \
        gstreamer1.0-alsa \
        gstreamer1.0-gl \
        gstreamer1.0-gtk3 \
        gstreamer1.0-qt5 \
        gstreamer1.0-pulseaudio
    
    # Video encoding
    sudo apt-get install -y \
        libx264-dev \
        libx265-dev \
        libvpx-dev \
        libopus-dev
    
    # Audio encoding
    sudo apt-get install -y \
        libfdk-aac-dev \
        libmp3lame-dev \
        libvorbis-dev
    
    # Video4Linux
    sudo apt-get install -y \
        v4l-utils \
        libv4l-dev
    
    # ALSA
    sudo apt-get install -y \
        libasound2-dev
    
    # PulseAudio
    sudo apt-get install -y \
        libpulse-dev
    
    print_success "Debian/Ubuntu dependencies installed"
}

# Install dependencies for Fedora/RHEL/CentOS
install_rhel() {
    print_info "Installing dependencies for Fedora/RHEL/CentOS..."
    
    # Build tools
    sudo dnf install -y \
        gcc \
        gcc-c++ \
        make \
        cmake \
        pkgconfig \
        git
    
    # GStreamer
    sudo dnf install -y \
        gstreamer1-devel \
        gstreamer1-plugins-base-devel \
        gstreamer1-plugins-good \
        gstreamer1-plugins-bad-free \
        gstreamer1-plugins-bad-freeworld \
        gstreamer1-plugins-ugly-free \
        gstreamer1-plugins-ugly-freeworld \
        gstreamer1-libav \
        gstreamer1-tools
    
    # Video encoding
    sudo dnf install -y \
        x264-devel \
        x265-devel \
        libvpx-devel \
        opus-devel
    
    # Audio encoding
    sudo dnf install -y \
        fdk-aac-devel \
        lame-devel \
        libvorbis-devel
    
    # Video4Linux
    sudo dnf install -y \
        v4l-utils \
        libv4l-devel
    
    # ALSA
    sudo dnf install -y \
        alsa-lib-devel
    
    # PulseAudio
    sudo dnf install -y \
        pulseaudio-libs-devel
    
    print_success "RHEL/Fedora dependencies installed"
}

# Install dependencies for Arch Linux
install_arch() {
    print_info "Installing dependencies for Arch Linux..."
    
    sudo pacman -Syu --noconfirm
    
    # Build tools
    sudo pacman -S --noconfirm \
        base-devel \
        cmake \
        pkg-config \
        git
    
    # GStreamer
    sudo pacman -S --noconfirm \
        gst-plugins-base \
        gst-plugins-good \
        gst-plugins-bad \
        gst-plugins-ugly \
        gst-libav \
        gstreamer-vaapi
    
    # Video encoding
    sudo pacman -S --noconfirm \
        x264 \
        x265 \
        libvpx \
        opus
    
    # Audio encoding
    sudo pacman -S --noconfirm \
        fdkaac \
        lame \
        libvorbis
    
    # Video4Linux
    sudo pacman -S --noconfirm \
        v4l-utils
    
    # ALSA
    sudo pacman -S --noconfirm \
        alsa-lib
    
    # PulseAudio
    sudo pacman -S --noconfirm \
        libpulse
    
    print_success "Arch Linux dependencies installed"
}

# Build the project
build_project() {
    print_info "Building the project..."
    
    # Create build directory
    mkdir -p build
    cd build
    
    # Configure with CMake
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local
    
    # Build
    make -j$(nproc)
    
    print_success "Build completed"
}

# Install the project
install_project() {
    print_info "Installing the project..."
    
    cd build
    sudo make install
    
    # Update library cache
    sudo ldconfig
    
    print_success "Installation completed"
}

# Verify installation
verify_installation() {
    print_info "Verifying installation..."
    
    # Check if binary exists
    if [ -f /usr/local/bin/media_framework ]; then
        print_success "Binary installed at /usr/local/bin/media_framework"
    else
        print_error "Binary not found"
        return 1
    fi
    
    # Check GStreamer plugins
    print_info "Checking GStreamer plugins..."
    
    local plugins=("x264enc" "x265enc" "aacenc" "opusenc" "mp4mux" "qtdemux")
    local missing=()
    
    for plugin in "${plugins[@]}"; do
        if ! gst-inspect-1.0 $plugin &> /dev/null; then
            missing+=("$plugin")
        fi
    done
    
    if [ ${#missing[@]} -eq 0 ]; then
        print_success "All required GStreamer plugins are available"
    else
        print_warning "Missing plugins: ${missing[*]}"
        print_warning "Some features may not work correctly"
    fi
    
    # Test run
    print_info "Testing binary..."
    if /usr/local/bin/media_framework --help &> /dev/null; then
        print_success "Binary runs correctly"
    else
        print_error "Binary test failed"
        return 1
    fi
}

# Main installation function
main() {
    echo "=============================================="
    echo "  GStreamer Media Framework Installer"
    echo "=============================================="
    echo ""
    
    check_root
    
    # Detect distribution
    DISTRO=$(detect_distro)
    print_info "Detected distribution: $DISTRO"
    
    # Install dependencies based on distribution
    case $DISTRO in
        ubuntu|debian|linuxmint|pop)
            install_debian
            ;;
        fedora|rhel|centos|rocky|almalinux)
            install_rhel
            ;;
        arch|manjaro|endeavouros)
            install_arch
            ;;
        *)
            print_error "Unsupported distribution: $DISTRO"
            print_info "Please install dependencies manually:"
            echo "  - GStreamer 1.14+ development libraries"
            echo "  - CMake 3.10+"
            echo "  - GCC or Clang"
            echo "  - x264, x265, opus, aac encoders"
            ;;
    esac
    
    # Build
    build_project
    
    # Install
    install_project
    
    # Verify
    verify_installation
    
    echo ""
    echo "=============================================="
    print_success "Installation completed successfully!"
    echo "=============================================="
    echo ""
    echo "Usage:"
    echo "  media_framework play <file>"
    echo "  media_framework record -o output.mp4"
    echo "  media_framework transcode -i input.mp4 -o output.mp4"
    echo "  media_framework devices"
    echo "  media_framework info <file>"
    echo ""
}

# Run main function
main "$@"
