# Formula of the tap Blackwolf1337/homebrew-quince (copied there as Formula/quince.rb)
class Quince < Formula
  desc "Native YouTube Music player (Qt Quick, libmpv)"
  homepage "https://github.com/Blackwolf1337/quince"
  url "https://github.com/Blackwolf1337/quince/archive/refs/tags/v0.1.1.tar.gz"
  sha256 "5bf2bcc9ac48939d56a2b33f9c8ca473e42010c9501521282441b50a40763ccc"
  # Quince; MeCab (BSD option); QuickJS-ng; the IPADIC dictionary has its own terms
  license all_of: ["GPL-3.0-or-later", "BSD-3-Clause", "MIT"]
  head "https://github.com/Blackwolf1337/quince.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  depends_on "ffmpeg"
  depends_on "icu4c"
  depends_on :macos
  depends_on "mpv"
  depends_on "qt"

  # what CMake would download, handed in so the build needs no network
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

  def install
    %w[mecab mecab-ipadic quickjs].each { |name| (buildpath/"deps"/name).install resource(name) }
    args = %W[
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON
      -DFETCHCONTENT_SOURCE_DIR_MECAB=#{buildpath}/deps/mecab
      -DFETCHCONTENT_SOURCE_DIR_MECAB_IPADIC=#{buildpath}/deps/mecab-ipadic
      -DFETCHCONTENT_SOURCE_DIR_QUICKJS=#{buildpath}/deps/quickjs
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
    with_env(QT_QPA_PLATFORM: "offscreen") do
      assert_match version.to_s, shell_output("#{bin}/quince --version")
    end
  end
end
