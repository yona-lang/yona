-- Haskell reference: binary file read + write + read-back
import qualified Data.ByteString as BS

main :: IO ()
main = do
    dat <- BS.readFile "bench/data/bench_binary.bin"
    BS.writeFile "/tmp/hs_bench_binary_wr.bin" dat
    dat2 <- BS.readFile "/tmp/hs_bench_binary_wr.bin"
    print (BS.length dat2)
