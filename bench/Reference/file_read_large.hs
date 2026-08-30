import qualified Data.ByteString as B
main :: IO ()
main = do
  bs <- B.readFile "bench/data/large_text.txt"
  print (B.length bs)
