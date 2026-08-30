import qualified Data.ByteString as B
main :: IO ()
main = do
  bs <- B.readFile "bench/data/large_text.txt"
  B.writeFile "/tmp/hs_bench_large_write.txt" bs
  bs2 <- B.readFile "/tmp/hs_bench_large_write.txt"
  print (B.length bs2)
