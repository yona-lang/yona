-- Haskell reference: parallel cube of 1..20
import Control.Parallel.Strategies
import Data.List (intercalate)

main :: IO ()
main = do
    let results = parMap rseq (\x -> x * x * x) [1..20 :: Int]
    putStrLn $ "[" ++ intercalate ", " (map show results) ++ "]"
