import qualified Data.ByteString as B
import Control.Concurrent.Async (mapConcurrently)
main :: IO ()
main = do
  let files = ["bench/data/chunk_1.txt", "bench/data/chunk_2.txt",
               "bench/data/chunk_3.txt", "bench/data/chunk_4.txt"]
  bss <- mapConcurrently B.readFile files
  print (sum (map B.length bss))
