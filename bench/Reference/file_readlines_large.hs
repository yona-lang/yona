import qualified Data.ByteString as B
import qualified Data.ByteString.Char8 as BC
main :: IO ()
main = do
  bs <- B.readFile "bench/data/large_text.txt"
  print (sum (map B.length (BC.lines bs)))
