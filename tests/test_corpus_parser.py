#!/usr/bin/env python3
"""Validation test suite for corpus event extractor (CLAUDE_RESEARCH.md Section 10.2).
Runs extract_corpus_events against hand-written C fixtures and verifies trace properties.
"""

import os
import sys
import unittest

# Add scripts/ to path to import CorpusParser
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "scripts")))
from extract_corpus_events import CorpusParser, compute_metrics, strip_comments_and_strings


class TestCorpusEventExtractor(unittest.TestCase):

    def setUp(self):
        self.fixture_path = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "fixtures", "sample_corpus_fixture.c")
        )
        self.assertTrue(os.path.exists(self.fixture_path), f"Fixture {self.fixture_path} missing")

    def test_strip_comments_and_strings(self):
        code = 'int a = 1; // comment\n/* multi\nline */ int b = "str"; int c = \'x\';'
        stripped = strip_comments_and_strings(code)
        self.assertNotIn("comment", stripped)
        self.assertNotIn("multi", stripped)
        self.assertNotIn("str", stripped)
        self.assertIn("int a = 1;", stripped)
        self.assertIn("int b =", stripped)

    def test_fixture_parsing(self):
        parser = CorpusParser()
        parser.parse_file(self.fixture_path)
        events = parser.events

        # 1. Verify event stream is non-empty
        self.assertGreater(len(events), 0)

        kinds = [ev[0] for ev in events]
        symbols = [ev[1] for ev in events if ev[0] in ("DECLARE", "USE")]

        # 2. Verify all event kinds exist
        self.assertIn("DECLARE", kinds)
        self.assertIn("USE", kinds)
        self.assertIn("ENTER_SCOPE", kinds)
        self.assertIn("EXIT_SCOPE", kinds)

        # 3. Verify specific symbols were captured
        self.assertIn("globalVar", symbols)
        self.assertIn("globalFunc", symbols)
        self.assertIn("paramA", symbols)
        self.assertIn("paramB", symbols)
        self.assertIn("localVar1", symbols)
        self.assertIn("localVar2", symbols)
        self.assertIn("innerVar", symbols)

        # 4. Check statistics
        metrics = compute_metrics("TestFixture", parser)
        self.assertEqual(metrics["files"], 1)
        self.assertGreater(metrics["declarations"], 0)
        self.assertGreater(metrics["uses"], 0)
        self.assertGreaterEqual(metrics["redeclarations"], 1)  # globalVar redeclared
        self.assertGreaterEqual(metrics["shadowing"], 1)      # inner localVar1 shadows outer
        self.assertGreaterEqual(metrics["max_scope_depth"], 2) # nested block depth >= 2

        print("\nFixture Validation Passed:")
        print(f"  Declarations: {metrics['declarations']}")
        print(f"  Uses:         {metrics['uses']}")
        print(f"  Redeclarations: {metrics['redeclarations']}")
        print(f"  Shadowing:    {metrics['shadowing']}")
        print(f"  Max Scope Depth: {metrics['max_scope_depth']}")


if __name__ == "__main__":
    unittest.main()
