import unittest
import errno
from flux.json_c import Jobj

json_str = '{ "a": 42 }'

class TestJsonCInterface(unittest.TestCase):
  def test_create_empty(self):
    """Create an empty json object"""
    j = Jobj()
    self.assertEqual(j.as_str(), '')

  def test_with_data(self):
    """Translate to and from json_object"""
    j = Jobj(json_str)
    self.assertEqual(
        j.as_str(),
        json_str
        )

if __name__ == '__main__':
      unittest.main()
