def test_hiwire_refs(selenium):
    selenium.run_js("pyodide.Tests.hiwire_tests.refs();")


def test_hiwire_int(selenium):
    selenium.run_js("pyodide.Tests.hiwire_tests.int();")


def test_hiwire_get_iter(selenium):
    selenium.run_js("pyodide.Tests.hiwire_tests.get_iter();")
