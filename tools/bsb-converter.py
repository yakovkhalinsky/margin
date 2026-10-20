#!/usr/bin/env python3
"""
Convert the Berean Standard Bible from eBible.org USFM to margin's Bible format.

Usage:
    python tools/bsb-converter.py --output /path/to/margin/bible/

The Berean Standard Bible is freely distributable for personal, church, or educational use.
Source: https://ebible.org/engbsb/
"""

import argparse
import json
import os
import re
import sys
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


BSB_USFM_URL = "https://ebible.org/Scriptures/engbsb_usfm.zip"

BOOKS = [
    ("GEN", "Genesis", 50),
    ("EXO", "Exodus", 40),
    ("LEV", "Leviticus", 27),
    ("NUM", "Numbers", 36),
    ("DEU", "Deuteronomy", 34),
    ("JOS", "Joshua", 24),
    ("JDG", "Judges", 21),
    ("RUT", "Ruth", 4),
    ("1SA", "1 Samuel", 31),
    ("2SA", "2 Samuel", 24),
    ("1KI", "1 Kings", 22),
    ("2KI", "2 Kings", 25),
    ("1CH", "1 Chronicles", 29),
    ("2CH", "2 Chronicles", 36),
    ("EZR", "Ezra", 10),
    ("NEH", "Nehemiah", 13),
    ("EST", "Esther", 10),
    ("JOB", "Job", 42),
    ("PSA", "Psalms", 150),
    ("PRO", "Proverbs", 31),
    ("ECC", "Ecclesiastes", 12),
    ("SNG", "Song of Solomon", 8),
    ("ISA", "Isaiah", 66),
    ("JER", "Jeremiah", 52),
    ("LAM", "Lamentations", 5),
    ("EZK", "Ezekiel", 48),
    ("DAN", "Daniel", 12),
    ("HOS", "Hosea", 14),
    ("JOL", "Joel", 3),
    ("AMO", "Amos", 9),
    ("OBA", "Obadiah", 1),
    ("JON", "Jonah", 4),
    ("MIC", "Micah", 7),
    ("NAH", "Nahum", 3),
    ("HAB", "Habakkuk", 3),
    ("ZEP", "Zephaniah", 3),
    ("HAG", "Haggai", 2),
    ("ZEC", "Zechariah", 14),
    ("MAL", "Malachi", 4),
    ("MAT", "Matthew", 28),
    ("MRK", "Mark", 16),
    ("LUK", "Luke", 24),
    ("JHN", "John", 21),
    ("ACT", "Acts", 28),
    ("ROM", "Romans", 16),
    ("1CO", "1 Corinthians", 16),
    ("2CO", "2 Corinthians", 13),
    ("GAL", "Galatians", 6),
    ("EPH", "Ephesians", 6),
    ("PHP", "Philippians", 4),
    ("COL", "Colossians", 4),
    ("1TH", "1 Thessalonians", 5),
    ("2TH", "2 Thessalonians", 3),
    ("1TI", "1 Timothy", 6),
    ("2TI", "2 Timothy", 4),
    ("TIT", "Titus", 3),
    ("PHM", "Philemon", 1),
    ("HEB", "Hebrews", 13),
    ("JAS", "James", 5),
    ("1PE", "1 Peter", 5),
    ("2PE", "2 Peter", 3),
    ("1JN", "1 John", 5),
    ("2JN", "2 John", 1),
    ("3JN", "3 John", 1),
    ("JUD", "Jude", 1),
    ("REV", "Revelation", 22),
]

USFM_BOOK_MAP = {
    "GEN": "01-GEN",
    "EXO": "02-EXO",
    "LEV": "03-LEV",
    "NUM": "04-NUM",
    "DEU": "05-DEU",
    "JOS": "06-JOS",
    "JDG": "07-JDG",
    "RUT": "08-RUT",
    "1SA": "09-1SA",
    "2SA": "10-2SA",
    "1KI": "11-1KI",
    "2KI": "12-2KI",
    "1CH": "13-1CH",
    "2CH": "14-2CH",
    "EZR": "15-EZR",
    "NEH": "16-NEH",
    "EST": "17-EST",
    "JOB": "18-JOB",
    "PSA": "19-PSA",
    "PRO": "20-PRO",
    "ECC": "21-ECC",
    "SNG": "22-SNG",
    "ISA": "23-ISA",
    "JER": "24-JER",
    "LAM": "25-LAM",
    "EZK": "26-EZK",
    "DAN": "27-DAN",
    "HOS": "28-HOS",
    "JOL": "29-JOL",
    "AMO": "30-AMO",
    "OBA": "31-OBA",
    "JON": "32-JON",
    "MIC": "33-MIC",
    "NAH": "34-NAH",
    "HAB": "35-HAB",
    "ZEP": "36-ZEP",
    "HAG": "37-HAG",
    "ZEC": "38-ZEC",
    "MAL": "39-MAL",
    "MAT": "41-MAT",
    "MRK": "42-MRK",
    "LUK": "43-LUK",
    "JHN": "44-JHN",
    "ACT": "45-ACT",
    "ROM": "46-ROM",
    "1CO": "47-1CO",
    "2CO": "48-2CO",
    "GAL": "49-GAL",
    "EPH": "50-EPH",
    "PHP": "51-PHP",
    "COL": "52-COL",
    "1TH": "53-1TH",
    "2TH": "54-2TH",
    "1TI": "55-1TI",
    "2TI": "56-2TI",
    "TIT": "57-TIT",
    "PHM": "58-PHM",
    "HEB": "59-HEB",
    "JAS": "60-JAS",
    "1PE": "61-1PE",
    "2PE": "62-2PE",
    "1JN": "63-1JN",
    "2JN": "64-2JN",
    "3JN": "65-3JN",
    "JUD": "66-JUD",
    "REV": "67-REV",
}


@dataclass
class Verse:
    chapter: int
    verse: int
    text: str


@dataclass
class Book:
    book_id: str
    name: str
    expected_chapters: int
    verses: list[Verse]

    def chapter_count(self) -> int:
        if not self.verses:
            return 0
        return max(v.chapter for v in self.verses)


def download_usfm(url: str, dest_dir: Path) -> Path:
    """Download and extract USFM zip file."""
    zip_path = dest_dir / "bsb_usfm.zip"
    print(f"Downloading {url}...")

    req = urllib.request.Request(url, headers={"User-Agent": "margin-bsb-converter/1.0"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        with open(zip_path, "wb") as f:
            f.write(resp.read())

    extract_dir = dest_dir / "usfm"
    print(f"Extracting to {extract_dir}...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(extract_dir)

    return extract_dir


def parse_usfm_file(path: Path) -> list[Verse]:
    """Parse a USFM file and extract verses."""
    verses = []
    current_chapter = 0
    current_verse = 0
    current_text = []

    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    content = re.sub(r"\\f\s+.*?\\f\*", "", content)
    content = re.sub(r"\\x\s+.*?\\x\*", "", content)
    content = re.sub(r"\\[a-z]+\*?", " ", content)
    content = re.sub(r"\s+", " ", content).strip()

    lines = content.split("\\")

    for line in lines:
        line = line.strip()
        if not line:
            continue

        if line.startswith("c "):
            if current_verse > 0 and current_text:
                text = " ".join(current_text).strip()
                if text:
                    verses.append(Verse(current_chapter, current_verse, text))
            match = re.match(r"c\s+(\d+)", line)
            if match:
                current_chapter = int(match.group(1))
            current_verse = 0
            current_text = []

        elif line.startswith("v "):
            if current_verse > 0 and current_text:
                text = " ".join(current_text).strip()
                if text:
                    verses.append(Verse(current_chapter, current_verse, text))
            match = re.match(r"v\s+(\d+)(?:-\d+)?\s*(.*)", line)
            if match:
                current_verse = int(match.group(1))
                rest = match.group(2).strip()
                current_text = [rest] if rest else []

        elif current_verse > 0:
            current_text.append(line)

    if current_verse > 0 and current_text:
        text = " ".join(current_text).strip()
        if text:
            verses.append(Verse(current_chapter, current_verse, text))

    return verses


def find_usfm_file(usfm_dir: Path, book_id: str) -> Optional[Path]:
    """Find the USFM file for a book, trying various naming conventions."""
    prefix = USFM_BOOK_MAP.get(book_id)
    if not prefix:
        return None

    for subdir in usfm_dir.iterdir():
        if subdir.is_dir():
            for pattern in [f"{prefix}*.usfm", f"{prefix}*.SFM", f"*{book_id}*.usfm"]:
                matches = list(subdir.glob(pattern))
                if matches:
                    return matches[0]

    for pattern in [f"{prefix}*.usfm", f"{prefix}*.SFM", f"*{book_id}*.usfm"]:
        matches = list(usfm_dir.glob(f"**/{pattern}"))
        if matches:
            return matches[0]

    return None


def convert_book(usfm_dir: Path, book_id: str, name: str, expected_chapters: int) -> Optional[Book]:
    """Convert a single book from USFM."""
    usfm_path = find_usfm_file(usfm_dir, book_id)
    if not usfm_path:
        print(f"  Warning: USFM file not found for {book_id} ({name})")
        return None

    print(f"  Converting {name} from {usfm_path.name}...")
    verses = parse_usfm_file(usfm_path)

    if not verses:
        print(f"  Warning: No verses found in {usfm_path}")
        return None

    book = Book(book_id, name, expected_chapters, verses)
    actual_chapters = book.chapter_count()

    if actual_chapters != expected_chapters:
        print(f"  Warning: {name} has {actual_chapters} chapters, expected {expected_chapters}")

    return book


def write_book_file(output_dir: Path, book: Book) -> Path:
    """Write a book to the margin text format."""
    path = output_dir / f"{book.book_id}.txt"

    current_chapter = 0
    with open(path, "w", encoding="utf-8") as f:
        for verse in sorted(book.verses, key=lambda v: (v.chapter, v.verse)):
            if verse.chapter != current_chapter:
                if current_chapter > 0:
                    f.write("\n")
                f.write(f"### {verse.chapter}\n")
                current_chapter = verse.chapter

            lines = wrap_verse_text(verse.text, 76)
            f.write(f"{verse.verse} {lines[0]}\n")
            for line in lines[1:]:
                f.write(f"  {line}\n")

    return path


def wrap_verse_text(text: str, width: int) -> list[str]:
    """Wrap verse text to specified width."""
    words = text.split()
    lines = []
    current_line = []
    current_len = 0

    for word in words:
        word_len = len(word)
        space_needed = 1 if current_line else 0

        if current_len + space_needed + word_len <= width:
            current_line.append(word)
            current_len += space_needed + word_len
        else:
            if current_line:
                lines.append(" ".join(current_line))
            current_line = [word]
            current_len = word_len

    if current_line:
        lines.append(" ".join(current_line))

    return lines if lines else [""]


def write_index(output_dir: Path, books: list[Book]) -> Path:
    """Write the index.json file."""
    path = output_dir / "index.json"

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

    with open(path, "w", encoding="utf-8") as f:
        json.dump(index, f, indent=2, ensure_ascii=False)

    return path


def verify_output(output_dir: Path) -> bool:
    """Verify the converted output."""
    index_path = output_dir / "index.json"
    if not index_path.exists():
        print("Error: index.json not found")
        return False

    with open(index_path, "r", encoding="utf-8") as f:
        index = json.load(f)

    books = index.get("books", [])
    if len(books) != 66:
        print(f"Warning: Expected 66 books, found {len(books)}")

    errors = 0
    for book_info in books:
        book_file = output_dir / book_info["file"]
        if not book_file.exists():
            print(f"Error: Missing book file {book_info['file']}")
            errors += 1
            continue

        chapters_found = set()
        with open(book_file, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("### "):
                    try:
                        ch = int(line[4:].strip())
                        chapters_found.add(ch)
                    except ValueError:
                        pass

        if len(chapters_found) != book_info["chapters"]:
            print(
                f"Warning: {book_info['id']} has {len(chapters_found)} chapters in file, "
                f"index says {book_info['chapters']}"
            )

    return errors == 0


def main():
    parser = argparse.ArgumentParser(
        description="Convert Berean Standard Bible from eBible.org to margin format"
    )
    parser.add_argument("--output", "-o", required=True, help="Output directory")
    parser.add_argument("--source", default=BSB_USFM_URL, help="USFM zip URL")
    parser.add_argument("--verify", action="store_true", help="Verify output after conversion")
    parser.add_argument("--keep-temp", action="store_true", help="Keep temporary download files")
    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)

        try:
            usfm_dir = download_usfm(args.source, temp_path)
        except Exception as e:
            print(f"Error downloading USFM: {e}")
            sys.exit(1)

        print("\nConverting books...")
        converted_books = []
        for book_id, name, chapters in BOOKS:
            book = convert_book(usfm_dir, book_id, name, chapters)
            if book:
                write_book_file(output_dir, book)
                converted_books.append(book)

        print(f"\nConverted {len(converted_books)} of {len(BOOKS)} books")

        print("\nWriting index.json...")
        write_index(output_dir, converted_books)

        total_size = sum(f.stat().st_size for f in output_dir.glob("*.txt"))
        total_size += (output_dir / "index.json").stat().st_size
        print(f"Total size: {total_size / 1024 / 1024:.2f} MB")

    if args.verify:
        print("\nVerifying output...")
        if verify_output(output_dir):
            print("Verification passed!")
        else:
            print("Verification found issues")
            sys.exit(1)

    print(f"\nOutput written to {output_dir}")
    print("Copy to your SD card at /margin/bible/")


if __name__ == "__main__":
    main()
