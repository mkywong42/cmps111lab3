hostname=$(hostname)
echo "Creating $HOME/$hostname.tar.gz"
rm ~/$hostname.tar 2>/dev/null
rm ~/$hostname.tar.gz 2>/dev/null
cd ../..

tar cvf ~/$hostname.tar \
	`diff -u -r ~/pintos ~/.pintos | lsdiff | sed 's/\/home\/pintos\/pintos\///' | sed '/src\/threads\/nbproject/d'` 2>/dev/null

tar rvf ~/$hostname.tar \
	`diff -r ~/.pintos ~/pintos | grep Only | sed 's/: /\//' | sed '/\/\.pintos/d' | sed 's/Only in //' | sed 's/\/home\/pintos\/pintos\///' | sed '/\/.git/d' | sed '/^>/d'`

gzip ~/$hostname.tar
