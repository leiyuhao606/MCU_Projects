#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rebuild words.h from dictionary.txt.

    python make_words.py

dictionary.txt holds the whole accepted word list as plain text, one five
letter lower case word per line, in two sections:

    [ANSWERS]  the answer pool - the secret word is picked from here, so
               every word in this section must be a word a player can
               reasonably be expected to know
    [EXTRAS]   further valid five letter words.  They are accepted as
               guesses but are never picked as the answer

The words are turned into 24 bit base-26 codes (26^5 = 11881376 < 2^24, so
every five letter combination has its own code), sorted, and stored delta +
varint compressed.  That costs about 1.7 bytes per word instead of the 5
bytes a plain letter string would need, which is what lets a dictionary of
several thousand words share the 8 KB flash with the program.

If you add words, watch the flash budget: compile afterwards and check the
"Program Size: ... code=" figure stays below 8192.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "dictionary.txt")
DST = os.path.join(HERE, "words.h")


def code(word):
    """24 bit base-26 code, 'a' = 0"""
    c = 0
    for ch in word:
        c = c * 26 + (ord(ch) - 97)
    return c


def encode(words):
    """sorted codes -> [first code big endian][varint gaps]"""
    cs = sorted(code(w) for w in words)
    out = [(cs[0] >> 16) & 0xFF, (cs[0] >> 8) & 0xFF, cs[0] & 0xFF]
    prev = cs[0]
    for c in cs[1:]:
        gap = c - prev
        prev = c
        groups = [gap & 0x7F]
        gap >>= 7
        while gap:
            groups.append(gap & 0x7F)
            gap >>= 7
        groups.reverse()
        for i, g in enumerate(groups):
            out.append(g | (0x80 if i < len(groups) - 1 else 0))
    return out


def emit(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join("0x%02X," % b for b in data[i:i + 16]))
    return "unsigned char code %s[%s_LEN] =\n{\n%s\n};\n" % (
        name, name, "\n".join(lines))


def main():
    answers, extras, section = [], [], None
    with open(SRC, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            low = line.lower()
            if low == "[answers]":
                section = answers
                continue
            if low == "[extras]":
                section = extras
                continue
            if not re.fullmatch(r"[a-z]{5}", low):
                sys.exit("dictionary.txt: not a five letter word: %r" % line)
            if section is None:
                sys.exit("dictionary.txt: word before any section: %r" % line)
            section.append(low)

    if not answers:
        sys.exit("dictionary.txt: the [ANSWERS] section is empty")
    for name, words in (("ANSWERS", answers), ("EXTRAS", extras)):
        if len(set(words)) != len(words):
            dup = sorted({w for w in words if words.count(w) > 1})
            sys.exit("dictionary.txt: duplicated words in %s: %s" % (name, dup[:10]))
    overlap = set(answers) & set(extras)
    if overlap:
        sys.exit("dictionary.txt: words in both sections: %s" % sorted(overlap)[:10])

    a, b = encode(answers), encode(extras)

    header = """#ifndef __WORDS_H__
#define __WORDS_H__

/*  Wordle dictionary - GENERATED FILE, do not edit by hand.
    Edit dictionary.txt and run:  python make_words.py

    DICT_A : %d answer words.  The secret word is picked from this table,
             so a correct guess can never be rejected as an unknown word.
    DICT_B : %d further valid words, accepted as guesses only.

    Both tables store 24 bit base-26 codes, sorted ascending and delta +
    varint compressed (about 1.7 bytes per word).
    A guess that is in neither table gets "Invalid word".
*/

#define ANSWER_COUNT    %d
#define EXTRA_COUNT     %d

#define DICT_A_LEN      %d
#define DICT_B_LEN      %d

%s
%s
#endif
""" % (len(answers), len(extras), len(answers), len(extras), len(a), len(b),
       emit("DICT_A", a), emit("DICT_B", b))

    with open(DST, "w", encoding="ascii") as fh:
        fh.write(header)

    print("answers %d -> %d bytes (%.2f/word)" % (len(answers), len(a), len(a) / len(answers)))
    print("extras  %d -> %d bytes (%.2f/word)" % (len(extras), len(b), len(b) / max(1, len(extras))))
    print("word data total %d bytes" % (len(a) + len(b)))
    print("words.h written")


if __name__ == "__main__":
    main()
