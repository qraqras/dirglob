require 'pp'
Dir.chdir('tests/fixtures') do
  pp Dir.glob('**/{a,b}.txt', sort: false)
end
