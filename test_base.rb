Dir.chdir('tests/fixtures')
puts "base: nil"
puts Dir.glob('{x,y,z}/.*', sort: true).inspect
puts "\nbase: '.'"
puts Dir.glob('{x,y,z}/.*', base: '.', sort: true).inspect
