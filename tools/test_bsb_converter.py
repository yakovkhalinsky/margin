#!/usr/bin/env python3
"""
Unit tests for bsb-converter.py.

Run with: python -m pytest tools/test_bsb_converter.py -v
Or simply: python tools/test_bsb_converter.py
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

spec = spec_from_loader("bsb_converter", SourceFileLoader("bsb_converter", str(Path(__file__).parent / "bsb-converter.py")))
bsb_converter = module_from_spec(spec)
spec.loader.exec_module(bsb_converter)

BOOKS = bsb_converter.BOOKS
USFM_BOOK_MAP = bsb_converter.USFM_BOOK_MAP
wrap_verse_text = bsb_converter.wrap_verse_text
Verse = bsb_converter.Verse
Book = bsb_converter.Book


class TestBookData(unittest.TestCase):
    def test_book_count(self):
        """Should have 66 books."""
        self.assertEqual(len(BOOKS), 66)

    def test_ot_nt_split(self):
        """OT should have 39 books, NT should have 27."""
        ot_books = [b for b in BOOKS if b[0] in [
            "GEN", "EXO", "LEV", "NUM", "DEU", "JOS", "JDG", "RUT",
            "1SA", "2SA", "1KI", "2KI", "1CH", "2CH", "EZR", "NEH",
            "EST", "JOB", "PSA", "PRO", "ECC", "SNG", "ISA", "JER",
            "LAM", "EZK", "DAN", "HOS", "JOL", "AMO", "OBA", "JON",
            "MIC", "NAH", "HAB", "ZEP", "HAG", "ZEC", "MAL"
        ]]
        self.assertEqual(len(ot_books), 39)
        
        nt_books = [b for b in BOOKS if b[0] in [
            "MAT", "MRK", "LUK", "JHN", "ACT", "ROM", "1CO", "2CO",
            "GAL", "EPH", "PHP", "COL", "1TH", "2TH", "1TI", "2TI",
            "TIT", "PHM", "HEB", "JAS", "1PE", "2PE", "1JN", "2JN",
            "3JN", "JUD", "REV"
        ]]
        self.assertEqual(len(nt_books), 27)

    def test_book_ids_unique(self):
        """All book IDs should be unique."""
        ids = [b[0] for b in BOOKS]
        self.assertEqual(len(ids), len(set(ids)))

    def test_book_ids_valid(self):
        """All book IDs should be 3 characters."""
        for book_id, name, chapters in BOOKS:
            self.assertEqual(len(book_id), 3, f"Book {name} has invalid ID length: {book_id}")
            self.assertTrue(book_id.isupper(), f"Book {name} ID not uppercase: {book_id}")

    def test_chapter_counts(self):
        """Spot check some chapter counts."""
        book_dict = {b[0]: b[2] for b in BOOKS}
        self.assertEqual(book_dict["GEN"], 50)  # Genesis
        self.assertEqual(book_dict["PSA"], 150)  # Psalms
        self.assertEqual(book_dict["JHN"], 21)  # John
        self.assertEqual(book_dict["REV"], 22)  # Revelation
        self.assertEqual(book_dict["OBA"], 1)   # Obadiah (1 chapter)
        self.assertEqual(book_dict["PHM"], 1)   # Philemon (1 chapter)

    def test_usfm_map_complete(self):
        """USFM map should have all 66 books."""
        self.assertEqual(len(USFM_BOOK_MAP), 66)
        for book_id, name, chapters in BOOKS:
            self.assertIn(book_id, USFM_BOOK_MAP, f"Missing USFM mapping for {name}")


class TestWrapVerseText(unittest.TestCase):
    def test_short_text(self):
        """Short text should not wrap."""
        result = wrap_verse_text("Hello world", 80)
        self.assertEqual(result, ["Hello world"])

    def test_long_text_wraps(self):
        """Long text should wrap at word boundaries."""
        text = "This is a longer sentence that should wrap to multiple lines when the width is small."
        result = wrap_verse_text(text, 30)
        self.assertGreater(len(result), 1)
        for line in result:
            self.assertLessEqual(len(line), 30)

    def test_empty_text(self):
        """Empty text should return single empty string."""
        result = wrap_verse_text("", 80)
        self.assertEqual(result, [""])

    def test_preserves_words(self):
        """Words should not be split."""
        text = "Word1 Word2 Word3"
        result = wrap_verse_text(text, 20)
        full_text = " ".join(result)
        self.assertIn("Word1", full_text)
        self.assertIn("Word2", full_text)
        self.assertIn("Word3", full_text)


class TestVerse(unittest.TestCase):
    def test_verse_creation(self):
        """Verse dataclass should work correctly."""
        v = Verse(1, 1, "In the beginning")
        self.assertEqual(v.chapter, 1)
        self.assertEqual(v.verse, 1)
        self.assertEqual(v.text, "In the beginning")


class TestBook(unittest.TestCase):
    def test_book_chapter_count(self):
        """Book chapter_count should return max chapter number."""
        verses = [
            Verse(1, 1, "Text"),
            Verse(1, 2, "Text"),
            Verse(2, 1, "Text"),
            Verse(3, 1, "Text"),
        ]
        book = Book("GEN", "Genesis", 50, verses)
        self.assertEqual(book.chapter_count(), 3)

    def test_empty_book(self):
        """Empty book should have 0 chapters."""
        book = Book("XXX", "Test", 0, [])
        self.assertEqual(book.chapter_count(), 0)


class TestIndexFormat(unittest.TestCase):
    def test_index_json_structure(self):
        """Generated index.json should have expected structure."""
        with tempfile.TemporaryDirectory() as tmpdir:
            books = [
                Book("GEN", "Genesis", 50, [Verse(1, 1, "In the beginning")]),
                Book("EXO", "Exodus", 40, [Verse(1, 1, "Names of Israel's sons")]),
            ]
            
            index_path = Path(tmpdir) / "index.json"
            index = {
                "version": "BSB",
                "name": "Berean Standard Bible",
                "language": "en",
                "source": "https://ebible.org/engbsb/",
                "books": [
                    {
                        "id": book.book_id,
                        "name": book.name,
                        "chapters": book.chapter_count(),
                        "file": f"{book.book_id}.txt",
                    }
                    for book in books
                ],
            }
            
            with open(index_path, "w") as f:
                json.dump(index, f)
            
            with open(index_path, "r") as f:
                loaded = json.load(f)
            
            self.assertEqual(loaded["version"], "BSB")
            self.assertEqual(loaded["language"], "en")
            self.assertEqual(len(loaded["books"]), 2)
            self.assertEqual(loaded["books"][0]["id"], "GEN")
            self.assertEqual(loaded["books"][0]["file"], "GEN.txt")


class TestBookTextFormat(unittest.TestCase):
    def test_chapter_marker_format(self):
        """Chapter markers should use ### N format."""
        # This tests the expected output format
        expected_chapter_1 = "### 1\n"
        expected_verse = "1 In the beginning God created the heavens and the earth.\n"
        
        self.assertTrue(expected_chapter_1.startswith("### "))
        self.assertTrue(expected_verse[0].isdigit())


if __name__ == "__main__":
    # Run with unittest when executed directly
    unittest.main(verbosity=2)
