safe :: Int -> [Int] -> Int -> Bool
safe _ [] _ = True
safe queen (q:rest) col
  | queen == q = False
  | abs (queen - q) == col = False
  | otherwise = safe queen rest (col + 1)

solve :: Int -> Int -> [Int] -> Int
solve n row placed
  | row > n = 1
  | otherwise = tryCol 1
  where
    tryCol col
      | col > n = 0
      | safe col placed 1 = solve n (row + 1) (col : placed) + tryCol (col + 1)
      | otherwise = tryCol (col + 1)

main :: IO ()
main = print (solve 10 1 [])
