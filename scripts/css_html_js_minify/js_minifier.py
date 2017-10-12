#!/usr/bin/env python3
# -*- coding: utf-8 -*-


"""JavaScript Minifier functions for CSS-HTML-JS-Minify."""


from io import StringIO  # pure-Python StringIO supports unicode.

import re

import logging as log

from .css_minifier import condense_semicolons


__all__ = ['js_minify']


def remove_commented_lines(js):
    """Force remove commented out lines from Javascript."""
    log.debug("Force remove commented out lines from Javascript.")
    result = ""
    for line in js.splitlines():
        line = re.sub(r"/\*.*\*/" ,"" ,line) # (/*COMMENT */)
        line = re.sub(r"//.*","" ,line) # (//COMMENT)
        result += '\n'+line
    return result


def simple_replacer_js(js):
    """Force strip simple replacements from Javascript."""
    log.debug("Force strip simple replacements from Javascript.")
    return condense_semicolons(js.replace("debugger;", ";").replace(
        ";}", "}").replace("; ", ";").replace(" ;", ";").rstrip("\n;"))


def js_minify_keep_comments(js):
    """Return a minified version of the Javascript string."""
    log.info("Compressing Javascript...")
    ins, outs = StringIO(js), StringIO()
    JavascriptMinify(ins, outs).minify()
    return force_single_line_js(outs.getvalue())


def force_single_line_js(js):
    """Force Javascript to a single line, even if need to add semicolon."""
    log.debug("Forcing JS from ~{0} to 1 Line.".format(len(js.splitlines())))
    return ";".join(js.splitlines()) if len(js.splitlines()) > 1 else js

class JavascriptMinify(object):
    """
    Minify an input stream of javascript, writing
    to an output stream
    """

    def __init__(self, instream=None, outstream=None, quote_chars="'\""):
        self.ins = instream
        self.outs = outstream
        self.quote_chars = quote_chars

    def minify(self, instream=None, outstream=None):
        if instream and outstream:
            self.ins, self.outs = instream, outstream

        self.is_return = False
        self.return_buf = ''

        def write(char):
            # all of this is to support literal regular expressions.
            # sigh
            if char in 'return':
                self.return_buf += char
                self.is_return = self.return_buf == 'return'
            else:
                self.return_buf = ''
                self.is_return = self.is_return and char < '!'
            self.outs.write(char)
            if self.is_return:
                self.return_buf = ''

        read = self.ins.read

        space_strings = "abcdefghijklmnopqrstuvwxyz"\
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$\\"
        self.space_strings = space_strings
        starters, enders = '{[(+-', '}])+-/' + self.quote_chars
        newlinestart_strings = starters + space_strings + self.quote_chars
        newlineend_strings = enders + space_strings + self.quote_chars
        self.newlinestart_strings = newlinestart_strings
        self.newlineend_strings = newlineend_strings

        do_newline = False
        do_space = False
        escape_slash_count = 0
        in_quote = ''
        quote_buf = []

        previous = ';'
        previous_non_space = ';'
        next1 = read(1)

        while next1:
            next2 = read(1)
            if in_quote:
                quote_buf.append(next1)

                if next1 == in_quote:
                    numslashes = 0
                    for c in reversed(quote_buf[:-1]):
                        if c != '\\':
                            break
                        else:
                            numslashes += 1
                    if numslashes % 2 == 0:
                        in_quote = ''
                        write(u''.join(quote_buf))
            elif next1 in '\r\n':
                next2, do_newline = self.newline(
                    previous_non_space, next2, do_newline)
            elif next1 < '!':
                if (previous_non_space in space_strings \
                    or previous_non_space > '~') \
                    and (next2 in space_strings or next2 > '~'):
                    do_space = True
                elif previous_non_space in '-+' and next2 == previous_non_space:
                    # protect against + ++ or - -- sequences
                    do_space = True
                elif self.is_return and next2 == '/':
                    # returning a regex...
                    write(u' ')
            elif next1 == '/':
                if do_space:
                    write(u' ')
                if next2 == '/':
                    # Line comment: treat it as a newline, but skip it
                    next2 = self.line_comment(next1, next2)
                    next1 = '\n'
                    next2, do_newline = self.newline(
                        previous_non_space, next2, do_newline)
                elif next2 == '*':
                    self.block_comment(next1, next2)
                    next2 = read(1)
                    if previous_non_space in space_strings:
                        do_space = True
                    next1 = previous
                else:
                    if previous_non_space in '{(,=:[?!&|;' or self.is_return:
                        self.regex_literal(next1, next2)
                        # hackish: after regex literal next1 is still /
                        # (it was the initial /, now it's the last /)
                        next2 = read(1)
                    else:
                        write(u'/')
            else:
                if do_newline:
                    write(u'\n')
                    do_newline = False
                    do_space = False
                if do_space:
                    do_space = False
                    write(u' ')

                write(next1)
                if next1 in self.quote_chars:
                    in_quote = next1
                    quote_buf = []

            if next1 >= '!':
                previous_non_space = next1

            if next1 == '\\':
                escape_slash_count += 1
            else:
                escape_slash_count = 0

            previous = next1
            next1 = next2

    def regex_literal(self, next1, next2):
        assert next1 == '/'  # otherwise we should not be called!

        self.return_buf = ''

        read = self.ins.read
        write = self.outs.write

        in_char_class = False

        write(u'/')

        next = next2
        while next and (next != '/' or in_char_class):
            write(next)
            if next == '\\':
                write(read(1))  # whatever is next is escaped
            elif next == '[':
                write(read(1))  # character class cannot be empty
                in_char_class = True
            elif next == ']':
                in_char_class = False
            next = read(1)

        write(u'/')

    def line_comment(self, next1, next2):
        assert next1 == next2 == '/'

        read = self.ins.read

        while next1 and next1 not in '\r\n':
            next1 = read(1)
        while next1 and next1 in '\r\n':
            next1 = read(1)

        return next1

    def block_comment(self, next1, next2):
        assert next1 == '/'
        assert next2 == '*'

        read = self.ins.read

        # Skip past first /* and avoid catching on /*/...*/
        next1 = read(1)
        next2 = read(1)

        comment_buffer = '/*'
        while next1 != '*' or next2 != '/':
            comment_buffer += next1
            next1 = next2
            next2 = read(1)

        if comment_buffer.startswith("/*!"):
            # comment needs preserving
            self.outs.write(comment_buffer)
            self.outs.write(u"*/\n")

    def newline(self, previous_non_space, next2, do_newline):
        read = self.ins.read

        if previous_non_space and (
                        previous_non_space in self.newlineend_strings
                        or previous_non_space > '~'):
            while 1:
                if next2 < '!':
                    next2 = read(1)
                    if not next2:
                        break
                else:
                    if next2 in self.newlinestart_strings \
                            or next2 > '~' or next2 == '/':
                        do_newline = True
                    break

        return next2, do_newline


def js_minify(js):
    """Minify a JavaScript string."""
    js = remove_commented_lines(js)
    js = js_minify_keep_comments(js)
    return js.strip()
