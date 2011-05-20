#!/bin/sh
# $FreeBSD: src/tools/regression/fstest/tests/chown/00.t,v 1.1 2007/01/17 01:42:08 pjd Exp $

desc="chown changes ownership"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..186"
else
	echo "1..171"
fi

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# super-user can always modify ownership
# 2
expect 0 create ${n0} 0644
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 unlink ${n0}
# 8
expect 0 mkfifo ${n0} 0644
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 unlink ${n0}
# 14
expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 123 456
expect 123,456 lstat ${n0} uid,gid
expect 0 chown ${n0} 0 0
expect 0,0 lstat ${n0} uid,gid
expect 0 rmdir ${n0}
# 20
expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect 0 chown ${n1} 123 456
expect 123,456 stat ${n1} uid,gid
expect 123,456 stat ${n0} uid,gid
expect 0 lchown ${n1} 135 579
expect 135,579 lstat ${n1} uid,gid
expect 123,456 stat ${n1} uid,gid
expect 123,456 stat ${n0} uid,gid
expect 0 unlink ${n0}
expect 0 unlink ${n1}

# non-super-user can modify file group if he is owner of a file and
# gid he is setting is in his groups list.
# 31
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
# 34
case "${fs}" in
ext3-diod)
	# group changes are not communicated to diod server for setattr
	expect 0 -u 65534 -g 65534 -- chown ${n0} -1 65534
	expect 65534,65534 lstat ${n0} uid,gid
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 65534,65534 lstat ${n0} uid,gid
	;;
*)
	expect 0 -u 65534 -g 65532,65531 -- chown ${n0} -1 65532
	expect 65534,65532 lstat ${n0} uid,gid
	expect 0 -u 65534 -g 65532,65531 chown ${n0} 65534 65531
	expect 65534,65531 lstat ${n0} uid,gid
	;;
esac
# 38
expect 0 unlink ${n0}

# chown(2) return 0 if user is not owner of a file, but chown(2) is called
# with both uid and gid equal to -1.
# 39
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 -u 65532 -g 65531 -- chown ${n0} -1 -1
expect 0 unlink ${n0}

# when super-user calls chown(2), set-uid and set-gid bits are not removed.
# 43
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 65532 65531
case "${os}" in
Linux)
	expect 0555 lstat ${n0} mode
        ;;
*)
	expect 06555 lstat ${n0} mode
        ;;
esac
expect 0 unlink ${n0}
# 50
expect 0 create ${n0} 0644
expect 0 chown ${n0} 0 0
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 65534 65533
case "${os}" in
Linux)
        expect 0555 lstat ${n0} mode
        ;;
*)
        expect 06555 lstat ${n0} mode
        ;;
esac

expect 0 unlink ${n0}
# 57
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 chown ${n0} 0 0
case "${os}" in
Linux)
        expect 0555 lstat ${n0} mode
        ;;
*)
        expect 06555 lstat ${n0} mode
        ;;
esac
expect 0 unlink ${n0}

# when non-super-user calls chown(2) successfully, set-uid and set-gid bits are
# removed, except when both uid and gid are equal to -1.
# 64
expect 0 create ${n0} 0644
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
# 68
case "${fs}" in
ext3-diod)
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 0555,65534,65534 lstat ${n0} mode,uid,gid
	;;
*)
	expect 0 -u 65534 -g 65533,65532 chown ${n0} 65534 65532
	expect 0555,65534,65532 lstat ${n0} mode,uid,gid
	;;
esac
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
# 72
case "${fs}" in
ext3-diod)
	expect 0 -u 65534 -g 65534 -- chown ${n0} -1 65534
	expect 0555,65534,65534 lstat ${n0} mode,uid,gid
	;;
*)
	expect 0 -u 65534 -g 65533,65532 -- chown ${n0} -1 65533
	expect 0555,65534,65533 lstat ${n0} mode,uid,gid
	;;
esac
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 -- chown ${n0} -1 -1
# 77
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0555,65534,65534 lstat ${n0} mode,uid,gid
	;;
Linux:*)
	expect 0555,65534,65533 lstat ${n0} mode,uid,gid
        ;;
*)
	expect 06555,65534,65533 lstat ${n0} mode,uid,gid
        ;;
esac

expect 0 unlink ${n0}
# 79
expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65533
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
# 83
case "${os}:${fs}" in
Linux:ext3|Linux:ntfs-3g|Linux:ZFS|Linux:ext3-diod)
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 06555,65534,65534 lstat ${n0} mode,uid,gid
        ;;
*)
	expect 0 -u 65534 -g 65533,65532 chown ${n0} 65534 65532
	expect 0555,65534,65532 lstat ${n0} mode,uid,gid
        ;;
esac
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
# 87
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0 -u 65534 -g 65534 -- chown ${n0} -1 65534
	expect 06555,65534,65534 lstat ${n0} mode,uid,gid
	;;
Linux:ext3|Linux:ntfs-3g|Linux:ZFS)
	expect 0 -u 65534 -g 65533,65532 -- chown ${n0} -1 65533
	expect 06555,65534,65533 lstat ${n0} mode,uid,gid
        ;;
*)
	expect 0 -u 65534 -g 65533,65532 -- chown ${n0} -1 65533
	expect 0555,65534,65533 lstat ${n0} mode,uid,gid
        ;;
esac
expect 0 chmod ${n0} 06555
expect 06555 lstat ${n0} mode
expect 0 -u 65534 -g 65533,65532 -- chown ${n0} -1 -1
# 92
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 06555,65534,65534 lstat ${n0} mode,uid,gid
	;;
*)
	expect 06555,65534,65533 lstat ${n0} mode,uid,gid
esac
expect 0 rmdir ${n0}
# 94
if supported lchmod; then
	expect 0 symlink ${n1} ${n0}
	expect 0 lchown ${n0} 65534 65533
	expect 0 lchmod ${n0} 06555
	expect 06555 lstat ${n0} mode
	expect 0 -u 65534 -g 65533,65532 lchown ${n0} 65534 65532
	expect 0555,65534,65532 lstat ${n0} mode,uid,gid
	expect 0 lchmod ${n0} 06555
	expect 06555 lstat ${n0} mode
	expect 0 -u 65534 -g 65533,65532 -- lchown ${n0} -1 65533
	expect 0555,65534,65533 lstat ${n0} mode,uid,gid
	expect 0 lchmod ${n0} 06555
	expect 06555 lstat ${n0} mode
	expect 0 -u 65534 -g 65533,65532 -- lchown ${n0} -1 -1
	expect 06555,65534,65533 lstat ${n0} mode,uid,gid
	expect 0 unlink ${n0}
fi

# successfull chown(2) call (except uid and gid equal to -1) updates ctime.
# 109 / 94
expect 0 create ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 114 / 99
expect 0 mkdir ${n0} 0755
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 rmdir ${n0}
# 119 / 104
expect 0 mkfifo ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 chown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 124 / 109
expect 0 symlink ${n1} ${n0}
ctime1=`${fstest} lstat ${n0} ctime`
sleep 1
expect 0 lchown ${n0} 65534 65533
expect 65534,65533 lstat ${n0} uid,gid
ctime2=`${fstest} lstat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 129 / 114
expect 0 create ${n0} 0644
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0 chown ${n0} 65534 65534
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 65534,65534 lstat ${n0} uid,gid
	;;
*)
	expect 0 chown ${n0} 65534 65533
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
	expect 65534,65532 lstat ${n0} uid,gid
	;;
esac
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 135 / 120
expect 0 mkdir ${n0} 0755
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0 chown ${n0} 65534 65534
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 65534,65534 lstat ${n0} uid,gid
	ctime2=`${fstest} stat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
	;;
*)
	expect 0 chown ${n0} 65534 65533
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
	expect 65534,65532 lstat ${n0} uid,gid
	ctime2=`${fstest} stat ${n0} ctime`
	;;
esac
expect 0 rmdir ${n0}
# 141 / 126
expect 0 mkfifo ${n0} 0644
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0 chown ${n0} 65534 65534
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 chown ${n0} 65534 65534
	expect 0 -u 65534 -g 65534 chown ${n0} 65534 65534
	expect 65534,65534 lstat ${n0} uid,gid
	;;
*)
	expect 0 chown ${n0} 65534 65533
	ctime1=`${fstest} stat ${n0} ctime`
	sleep 1
	expect 0 chown ${n0} 65534 65533
	expect 0 -u 65534 -g 65532 chown ${n0} 65534 65532
	expect 65534,65532 lstat ${n0} uid,gid
	;;
esac
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 148 / 133
expect 0 symlink ${n1} ${n0}
case "${os}:${fs}" in
Linux:ext3-diod)
	expect 0 lchown ${n0} 65534 65534
	ctime1=`${fstest} lstat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65534 lchown ${n0} 65534 65534
	expect 65534,65534 lstat ${n0} uid,gid
	;;
*)
	expect 0 lchown ${n0} 65534 65533
	ctime1=`${fstest} lstat ${n0} ctime`
	sleep 1
	expect 0 -u 65534 -g 65532 lchown ${n0} 65534 65532
	expect 65534,65532 lstat ${n0} uid,gid
	;;
esac
ctime2=`${fstest} lstat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}
# 154 / 139
expect 0 create ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 -- chown ${n0} -1 -1
ctime2=`${fstest} stat ${n0} ctime`
case "${os}:${fs}" in
Linux:ext3|Linux:ZFS|Linux:ext3-diod)
	test_check $ctime1 -lt $ctime2
        ;;
*)
	test_check $ctime1 -eq $ctime2
        ;;
esac
expect 0 unlink ${n0}
# 158 / 143
expect 0 mkdir ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 -- chown ${n0} -1 -1
ctime2=`${fstest} stat ${n0} ctime`
case "${os}:${fs}" in
Linux:ext3|Linux:ZFS|Linux:ext3-diod)
	test_check $ctime1 -lt $ctime2
        ;;
*)
	test_check $ctime1 -eq $ctime2
        ;;
esac
expect 0 rmdir ${n0}
# 162 / 147
expect 0 mkfifo ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 -- chown ${n0} -1 -1
ctime2=`${fstest} stat ${n0} ctime`
case "${os}:${fs}" in
Linux:ext3|Linux:ZFS|Linux:ext3-diod)
	test_check $ctime1 -lt $ctime2
        ;;
*)
	test_check $ctime1 -eq $ctime2
        ;;
esac
expect 0 unlink ${n0}
# 166 / 151
expect 0 symlink ${n1} ${n0}
ctime1=`${fstest} lstat ${n0} ctime`
sleep 1
expect 0 -- lchown ${n0} -1 -1
ctime2=`${fstest} lstat ${n0} ctime`
case "${os}:${fs}" in
Linux:ext3|Linux:ZFS|Linux:ext3-diod)
	test_check $ctime1 -lt $ctime2
        ;;
*)
	test_check $ctime1 -eq $ctime2
        ;;
esac
expect 0 unlink ${n0}

# unsuccessful chown(2) does not update ctime.
# 170 / 155
expect 0 create ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -- chown ${n0} 65534 -1
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 174 / 159
expect 0 mkdir ${n0} 0755
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 -- chown ${n0} -1 65534
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 rmdir ${n0}
# 178 / 163
expect 0 mkfifo ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 chown ${n0} 65534 65534
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}
# 182 / 167
expect 0 symlink ${n1} ${n0}
ctime1=`${fstest} lstat ${n0} ctime`
sleep 1
expect EPERM -u 65534 -g 65534 lchown ${n0} 65534 65534
ctime2=`${fstest} lstat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

# 186 / 171
cd ${cdir}
expect 0 rmdir ${n2}
