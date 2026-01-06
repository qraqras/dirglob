def test(pat, str, flags = 0)
  res = File.fnmatch(pat, str, flags)
  puts "fnmatch('#{pat}', '#{str}', #{flags}) #=> #{res}"
end

test('a/**/*', 'a/b/c', File::FNM_PATHNAME)
test('a/**', 'a/b/c', File::FNM_PATHNAME)
test('**/a', 'b/c/a', File::FNM_PATHNAME)
test('a/**b', 'a/x/y/b', File::FNM_PATHNAME)
test('a/xx**/b', 'a/xx/y/b', File::FNM_PATHNAME)
