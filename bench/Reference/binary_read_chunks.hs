-- Haskell reference: binary chunked read
import System.IO
import qualified Data.ByteString as BS

main :: IO ()
main = do
    h <- openBinaryFile "bench/data/bench_binary.bin" ReadMode
    total <- readChunks h 0
    hClose h
    print total
  where
    readChunks h acc = do
        chunk <- BS.hGet h 65536
        if BS.null chunk
            then return acc
            else readChunks h (acc + BS.length chunk)
