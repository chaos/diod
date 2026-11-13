##
# Is non-interactive sudo available?
##
if sudo --non-interactive true >/dev/null 2>&1; then
    test_set_prereq SUDO
    #  Do not use -E: the security policy may refuse to preserve the
    #  environment, and sudo only warns when it does, e.g. on the github
    #  runners.  Tests must pass what they need on the command line.
    SUDO="sudo"
elif _probeenv=xyz doas -n printenv _probeenv >/dev/null 2>&1; then
    test_set_prereq SUDO
    SUDO="doas"
fi
##
# Fixup sudo commandline if sanitizers are enabled.
# LSan doesn't work under setuid or program run under sudo
##
if test_have_prereq ASAN; then
    SUDO="ASAN_OPTIONS=$ASAN_OPTIONS:detect_leaks=0 $SUDO"
fi
