#!/usr/bin/perl

use strict;
use warnings;
use IO::Handle;
use File::Copy;
use File::Compare;

our $tmp1 = "tmp1.$$";
our $tmp2 = "tmp2.$$";

END {
	my $status = $?;
	unlink($tmp1, $tmp2);
	$? = $status;
}

# Move $src to $dest, adding an "AUTOGENERATED FILE" warning on top.
sub move_add_warning {
	my $src = shift;
	my $dest = shift;
	
	open(RD, "<", $src) || die "Can't read $src";
	open(WR, ">", $dest) || die "Can't write $dest";
	print WR "/* AUTOGENERATED FILE -- DO NOT EDIT */\n";
	WR->flush();
	copy(\*RD, \*WR) || die "Can't write $dest";
	close RD;
	close WR;
	unlink($src);
}

# Move $src to $dest, unless the contents of $src and $dest are identical,
# in which case delete $src.
sub cond_move {
	my $src = shift;
	my $dest = shift;
	
	if (compare($src, $dest)) {
		rename($src, $dest);
	} else {
		unlink($src);
	}
}

sub handle_subdir {
	my $dirname = shift;
	my $varname = shift;
	my $files = shift;
	
	my $assign = " =";
	my $file;
	
	open(FH, ">", $tmp1) || die "Couldn't open $tmp1";
	print FH "# AUTOGENERATED FILE -- DO NOT EDIT\n";
	foreach $file (@$files) {
		next unless -f $file;
		move_add_warning($file, $tmp2);
		cond_move($tmp2, "$dirname/$file");
		print FH "$varname $assign $file\n";
		$assign = "+=";
	}
	close FH;
	cond_move($tmp1, "$dirname/$dirname.mk");
}


my $compiler = $ENV{"ASN1C"} ? $ENV{"ASN1C"} : "asn1c";

if ((system("$compiler 2>/dev/null") >> 8) == 127) {
	print STDERR <<EOF;
Error: Can't find asn1c.  Try adding it to your PATH or setting the ASN1C
environment variable.
EOF
	exit 1;
}

my @generated;
my @copied;
my $command = "$compiler -Werror -fskeletons-copy";
my $file;
for $file (@ARGV) {
	$command .= " $file";
}
open(COMP, "-|", "$command 2>&1")
	or die "Couldn't run asn1c";
while (<COMP>) {
	if (/^Copied [^\t ]+[\t ]+-> ([a-zA-Z0-9._-]+)$/) {
		push(@copied, $1);
		next;
	}
	if (/^Compiled ([a-zA-Z0-9.-]+)/) {
		push(@generated, $1);
		next;
	}
	next if (/^Generated /);
	print STDERR;
}
close COMP;
exit $? >> 8 if $? >> 8;

# XXX autoremove anything not in the set

unlink("Makefile.am.sample", "converter-sample.c");

handle_subdir("classes", "CLASS_FILES", \@generated);
handle_subdir("support", "SUPPORT_FILES", \@copied);

open(FH, ">stamp") || die "Can't touch stamp file";
close FH;

exit 0;
