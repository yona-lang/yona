# frozen_string_literal: true

class Yona < Formula
  desc "Yona programming language compiler targeting LLVM"
  homepage "https://github.com/yona-lang/yonac-llvm"
  version "0.1.2"
  url "https://github.com/yona-lang/yonac-llvm/archive/refs/tags/v#{version}.tar.gz"
  sha256 "95fbb828fea7b6913487792f53eeb79e810f8d6e90f7d93ead38fd04c8946bc6"
  license "GPL-3.0-only"
  head "https://github.com/yona-lang/yonac-llvm.git", branch: "master"

  livecheck do
    url :stable
    regex(/^v?(\d+(?:\.\d+)+)$/i)
  end

  option "with-vulkan", "Enable Std\\GPU Vulkan support (MoltenVK on macOS)"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  depends_on "cli11"
  depends_on "lld"
  depends_on "llvm"
  depends_on "pcre2"

  uses_from_macos "libxml2"
  uses_from_macos "zlib"

  depends_on "vulkan-headers" => :build if build.with?("vulkan")
  depends_on "vulkan-loader" if build.with?("vulkan")
  depends_on "molten-vk" if OS.mac? && build.with?("vulkan")

  def install
    llvm = Formula["llvm"]
    lld = Formula["lld"]

    ENV["CC"] = llvm.opt_bin/"clang"
    ENV["CXX"] = llvm.opt_bin/"clang++"
    ENV["LLVM_INSTALL_PREFIX"] = llvm.opt_prefix
    ENV.prepend_path "PATH", llvm.opt_bin
    ENV.prepend_path "PATH", lld.opt_bin
    ENV.append "LDFLAGS", "-fuse-ld=lld"

    args = %W[
      -GNinja
      -DBUILD_TESTING=OFF
      -DYONA_FETCH_DEPS=OFF
      -DYONA_FETCH_LIBXML2=OFF
      -DYONA_ENABLE_INPROCESS_LLD=ON
      -DLLVM_DIR=#{llvm.opt_lib}/cmake/llvm
      -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
      -DCMAKE_INSTALL_RPATH=#{rpath};#{llvm.opt_lib}
    ]
    args << if OS.mac?
      "-DCMAKE_TOOLCHAIN_FILE=#{buildpath}/cmake/homebrew-llvm-toolchain.cmake"
    else
      "-DCMAKE_TOOLCHAIN_FILE=#{buildpath}/cmake/linux-llvm-toolchain.cmake"
    end
    args << "-DYONA_ENABLE_VULKAN=ON" if build.with?("vulkan")

    system "cmake", "-S", ".", "-B", "build", *std_cmake_args, *args
    system "cmake", "--build", "build"

    lib.install "build/#{shared_library("yona_lib")}"
    libexec.install "build/yonac", "build/yona"

    sysroot = lib/"yona"
    (sysroot/"lib").mkpath
    cp_r "lib/Std", sysroot/"lib"
    (sysroot/"lib").install "lib/Prelude.yona", "lib/Prelude.yonai"

    (sysroot/"src").install "src/compiled_runtime.c"
    (sysroot/"src/runtime").install Dir["src/runtime/*.c", "src/runtime/*.h"]
    (sysroot/"src/runtime/platform").install Dir["src/runtime/platform/*"]
    (sysroot/"include/yona/runtime").install Dir["include/yona/runtime/*.h"]
    (sysroot/"runtime").install Dir["build/runtime/*"]

    env = {
      PATH:       "#{llvm.opt_bin}:#{lld.opt_bin}:\$PATH",
      YONA_HOME:  opt_lib/"yona",
      YONAC_CC:   llvm.opt_bin/"clang",
    }
    (bin/"yonac").write_env_script libexec/"yonac", env
    (bin/"yona").write_env_script libexec/"yona", env
  end

  def caveats
    <<~EOS
      LLVM is keg-only. The `yonac` and `yona` wrappers add Homebrew LLVM and LLD
      to PATH and set YONA_HOME / YONAC_CC so compiling programs works out of the box.

      Optional Vulkan GPU runtime:
        brew install akovari/tap/yona --with-vulkan
    EOS
  end

  test do
    assert_match(/\d+\.\d+\.\d+/, shell_output("#{bin}/yonac --version"))
    assert_equal "42\n", shell_output("#{bin}/yonac -e '42'")
  end
end
