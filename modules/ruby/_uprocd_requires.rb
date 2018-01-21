require 'pathname'

$:.each do |path|
  if path.include? "/ruby/#{RUBY_VERSION}"
    paths = Pathname.glob("#{path}/*.{rb,so}")
    paths
      .map{|p| p.basename.sub_ext ''}
      .map(&:to_s)
      .select{|req| !['continuation', 'debug', 'profile'].include? req }
      .each{|req| require req}
  end
end
