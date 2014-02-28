require 'formula'

class PhpQb < Formula
  homepage 'http://www.php-qb.net/'
  url 'https://bitbucket.org/chung_leong/qb/downloads/qb-2.1-source-code.tar.bz2'
  sha1 '1714591973ec082e7a902374658e951be0d4d245'

  depends_on 'autoconf' => :build
  
  def install
    system "phpize"
    system "./configure"
    system "make"
    system "cp", "modules/qb.so", "#{prefix}/qb.so"
    system "cp", "qb.ini", "#{prefix}/qb.ini"    
  end

  test do
    system "echo \"<?php exit(extension_loaded('qb') || dl('qb.so') ? 0 : -1); ?>\" | php"
  end
  
  fails_with :clang do
    build 425
    cause "Compiling with clang is incredibly slow"
  end
end