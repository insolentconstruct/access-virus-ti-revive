"""The website's "Thank you" list and the README's "Thank you" section are the same text.
Run: python3 -I -m unittest discover -s scripts/tests"""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sitebuild", os.path.join(HERE, "..", "site", "build.py"))
sitebuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sitebuild)


class Thanks(unittest.TestCase):
    def test_readme_section_is_the_site_list(self):
        with open(os.path.join(HERE, "..", "..", "README.md"), encoding="utf-8") as f:
            readme = f.read()
        self.assertIn("## Thank you\n\n" + sitebuild.thanks_markdown() + "\n## ", readme)

    def test_issue_numbers_become_links(self):
        html = sitebuild.thanks_block()
        self.assertIn('<a href="https://github.com/packman83">packman83</a>', html)
        self.assertIn('<a href="' + sitebuild.GH + '/issues/37">#37</a>', html)


if __name__ == "__main__":
    unittest.main()
