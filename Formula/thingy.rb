class Thingy < Formula
  desc "Sakura-themed TUI editor with code execution"
  homepage "https://github.com/Velox0/thingy"
  url "https://github.com/Velox0/thingy/archive/refs/tags/v1.0.2.tar.gz"
  sha256 "bc2142745ab2397a2d2a9f7c851626c0410051db388b2fd6e32a15f6bda93e26"
  license "Unlicense"

  depends_on "ncurses"
  depends_on "curl"

  def install
    system "make"
    bin.install "build/bin/thingy"
  end

  test do
    system "#{bin}/thingy", "--help"
  end
end
