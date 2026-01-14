
Dir.chdir("../tests/fixtures") do
  puts "p0103 (**/?):"
  puts Dir.glob("**/?").sort
  puts "---"
  puts "p0118 (**/**/):"
  puts Dir.glob("**/**/").sort
end
