#!/bin/bash

# Script to install necessary dependencies for the file-sharing-app

set -e # Exit immediately if a command exits with a non-zero status.

echo "Detecting operating system..."

if [ -f /etc/debian_version ]; then
    # Debian, Ubuntu, Mint, etc.
    echo "Detected Debian-based OS. Using apt-get."
    sudo apt-get update
    sudo apt-get install -y build-essential pkg-config libssl-dev libgtk-3-dev

elif [ -f /etc/redhat-release ]; then
    # Fedora, CentOS, RHEL
    echo "Detected Red Hat-based OS. Using dnf/yum."
    if command -v dnf &> /dev/null; then
        sudo dnf install -y gcc make pkgconfig openssl-devel gtk3-devel
    else
        sudo yum install -y gcc make pkgconfig openssl-devel gtk3-devel
    fi

elif [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    echo "Detected macOS. Using Homebrew."
    if ! command -v brew &> /dev/null; then
        echo "Homebrew not found. Please install it first from https://brew.sh"
        exit 1
    fi
    brew install pkg-config openssl gtk+3

else
    echo "Unsupported operating system. Please install 'build-essential' (or equivalent) and 'libssl-dev' (or equivalent) and 'libgtk-3-dev' (or equivalent) manually."
    exit 1
fi

echo "Dependencies installed successfully!"