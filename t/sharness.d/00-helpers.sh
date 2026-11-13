##
# Extra assertions, in the style of sharness's own test_must_be_empty().
##

# Check that a file matches a pattern, and barf with the file's contents
# otherwise.  Use in place of grep when a failure is easier to understand
# if the actual text is shown, such as an error message that may
# legitimately vary.
#
# usage: test_grep pattern file
test_grep() {
    if ! grep "$1" "$2"
    then
        echo "'$2' does not match '$1', it contains:"
        cat "$2"
        return 1
    fi
}
