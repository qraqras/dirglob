def test(pat, str, flags = 0)
  res = File.fnmatch(pat, str, flags)
  puts "fnmatch('#{pat}', '#{str}', #{flags}) #=> #{res}"
end

puts "--- Basics ---"
test('a', 'a')
test('a', 'b')
test('*', 'a/b')
test('*', 'a/b', File::FNM_PATHNAME)

puts "--- Asterisks ---"
test('**', 'a/b/c')
test('**', 'a/b/c', File::FNM_PATHNAME)
test('**/a', 'a', File::FNM_PATHNAME)
test('**/a', 'b/a', File::FNM_PATHNAME)
test('**/a', 'b/c/a', File::FNM_PATHNAME)
test('a/**', 'a', File::FNM_PATHNAME)
test('a/**', 'a/b', File::FNM_PATHNAME)
test('a/**', 'a/b/c', File::FNM_PATHNAME)

puts "--- Multiple Asterisks ---"
test('***', 'a/b/c')
test('***', 'a/b/c', File::FNM_PATHNAME)

puts "--- Dots ---"
test('*', '.a')
test('*', '.a', File::FNM_DOTMATCH)
test('*/a', '.a/a', File::FNM_PATHNAME)
test('*/a', '.a/a', File::FNM_PATHNAME | File::FNM_DOTMATCH)
test('**/a', '.a/a', File::FNM_PATHNAME)
test('**/a', '.a/a', File::FNM_PATHNAME | File::FNM_DOTMATCH)

puts "--- Brackets ---"
test('[a-z]', 'a')
test('[^a-z]', '1')
test('[!a-z]', '1')
test('[[:alpha:]]', 'a')
test('[a\-z]', '-')
test('[a-z]', '-') # check range
test('[\]]', ']')

puts "--- Edge cases ---"
test('', '')
test('', 'a')
test('a', '')
test("\\", "\\")
test("\\", "\\", File::FNM_NOESCAPE)
test("a\\", "a")
test("a\\", "a\\", File::FNM_NOESCAPE)
test("a\0b", "a\0b") rescue puts "Caught null char error"
