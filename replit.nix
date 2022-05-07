{ pkgs }: {
	deps = [
		pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
        pkgs.less
        pkgs.lua5_4
        pkgs.rlwrap
        pkgs.vim
        pkgs.verilog
        pkgs.gtkwave
	];
}