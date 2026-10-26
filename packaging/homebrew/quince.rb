# Formula of the tap Blackwolf1337/homebrew-quince (copied there as Formula/quince.rb)
class Quince < Formula
  desc "Native YouTube Music player (Slint, libmpv)"
  homepage "https://github.com/Blackwolf1337/quince"
  url "https://github.com/Blackwolf1337/quince/archive/refs/tags/v0.3.0.tar.gz"
  sha256 "5e7035c491f7996f90c300f27029bc2c4d2b083ef0616e440d8c4922696093c2"
  # Quince; MeCab (BSD option); QuickJS-ng; the IPADIC dictionary has its own terms
  license all_of: ["GPL-3.0-or-later", "BSD-3-Clause", "MIT"]
  head "https://github.com/Blackwolf1337/quince.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  depends_on "rust" => :build
  depends_on "ffmpeg"
  depends_on "icu4c"
  depends_on :macos
  depends_on "mpv"
  depends_on "qt"

  # what CMake would download, handed in (cargo still fetches Slint's crates)
  resource "mecab" do
    url "https://github.com/shogo82148/mecab/releases/download/v0.996.13/mecab-0.996.13.tar.gz"
    sha256 "3da28541062d6d1cfbb92ab09481779f2ad735c89705d981eb66f072ef776538"
  end

  resource "mecab-ipadic" do
    url "https://github.com/shogo82148/mecab/releases/download/v0.996.13/mecab-ipadic-2.7.0-20070801.tar.gz"
    sha256 "74130f44264ce5b8cfa51e498b99345f71d2f854b74a9e9dfb6489e13e479e67"
  end

  resource "quickjs" do
    url "https://github.com/quickjs-ng/quickjs/archive/refs/tags/v0.17.0.tar.gz"
    sha256 "559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9"
  end

  resource "slint" do
    url "https://github.com/slint-ui/slint/archive/refs/tags/v1.18.1.tar.gz"
    sha256 "fe485305ed303215e76c04918ee9aefbffbe229f18f979098ec36c7fa1dab28b"
  end

  # Slint's CMake glue for cargo
  resource "corrosion" do
    url "https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz"
    sha256 "e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3"
  end

  resource "nfd" do
    url "https://github.com/btzy/nativefiledialog-extended/archive/refs/tags/v1.4.1.tar.gz"
    sha256 "4a01eafa921169f009f3e83bea8e287d9e5d0b94ca9064cf7f0a546652d7c884"
  end

  def install
    %w[mecab mecab-ipadic quickjs slint corrosion nfd].each { |name| (buildpath/"deps"/name).install resource(name) }
    # Quince's changes to Slint: FetchContent applies them only to a Slint it downloads itself
    cd buildpath/"deps/slint" do
      system "cmake", "-P", buildpath/"cmake/SlintPatch.cmake"
    end
    args = %W[
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON
      -DFETCHCONTENT_SOURCE_DIR_MECAB=#{buildpath}/deps/mecab
      -DFETCHCONTENT_SOURCE_DIR_MECAB_IPADIC=#{buildpath}/deps/mecab-ipadic
      -DFETCHCONTENT_SOURCE_DIR_QUICKJS=#{buildpath}/deps/quickjs
      -DFETCHCONTENT_SOURCE_DIR_SLINT=#{buildpath}/deps/slint
      -DFETCHCONTENT_SOURCE_DIR_CORROSION=#{buildpath}/deps/corrosion
      -DFETCHCONTENT_SOURCE_DIR_NFD=#{buildpath}/deps/nfd
    ]
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja", *args, *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
    # Quince.app lands in the prefix; the command starts the app's binary
    # (a script, not a symlink: the app finds its resources from its real path)
    (bin/"quince").write <<~SH
      #!/bin/bash
      exec "#{opt_prefix}/Quince.app/Contents/MacOS/Quince" "$@"
    SH
    chmod 0755, bin/"quince"
  end

  def caveats
    <<~EOS
      Quince.app was installed to:
        #{opt_prefix}/Quince.app
      To find it in Launchpad and Spotlight, link it once:
        ln -sf #{opt_prefix}/Quince.app ~/Applications/Quince.app
    EOS
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/quince --version")
  end
end
