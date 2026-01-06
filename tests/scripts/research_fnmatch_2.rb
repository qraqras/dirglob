def test(pat, str, flags = 0)
  res = File.fnmatch(pat, str, flags)
  puts "fnmatch('#{pat}', '#{str}', #{flags}) #=> #{res}"
end

puts "--- Recursive double asterisk with FNM_PATHNAME ---"
test('**/a', 'a', File::FNM_PATHNAME)
test('**/a', 'b/a', File::FNM_PATHNAME)
test('**/a', 'b/c/a', File::FNM_PATHNAME)
test('a/**', 'a/b', File::FNM_PATHNAME)
test('a/**', 'a/b/c', File::FNM_PATHNAME)
test('a/**/b', 'a/b', File::FNM_PATHNAME)
test('a/**/b', 'a/x/b', File::FNM_PATHNAME)
test('a/**/b', 'a/x/y/b', File::FNM_PATHNAME)

puts "--- Double asterisk without slash ---"
test('a**b', 'ab', File::FNM_PATHNAME)
test('a**b', 'axb', File::FNM_PATHNAME)
test('a**b', 'a/b', File::FNM_PATHNAME)
test('a**b', 'a/x/b', File::FNM_PATHNAME)

puts "--- Triple asterisk ---"
test('***', 'a/b/c', File::FNM_PATHNAME)
test('a/***', 'a/b/c', File::FNM_PATHNAME)
test('**/*', 'a/b/c', File::FNM_PATHNAME)
