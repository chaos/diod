##
# Is non-interactive sudo available?
##
if sudo --non-interactive true >/dev/null 2>&1; then
    test_set_prereq SUDO
fi
##
# Fixup sudo commandline if sanitizers are enabled.
# LSan doesn't work under setuid or program run under sudo
##
SUDO=sudo
if test_have_prereq ASAN; then
    SUDO="sudo -E ASAN_OPTIONS=$ASAN_OPTIONS:detect_leaks=0"
fi
