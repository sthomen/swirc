function get_file(url, name)
{
    var http_req = new ActiveXObject("WinHttp.WinHttpRequest.5.1");
    var stream   = new ActiveXObject("ADODB.Stream");

    http_req.Open("GET", url, false);
    http_req.Send();
    http_req.WaitForResponse();

    stream.Type = 1;
    stream.Open();
    stream.Write(http_req.ResponseBody);
    stream.SaveToFile(name);
}

get_file("http://nifty-networks.net/swirc/curl-7.56.1.cab", "curl-7.56.1.cab");
get_file("http://nifty-networks.net/swirc/libressl-2.6.3-windows.cab", "libressl-2.6.3-windows.cab");
get_file("http://nifty-networks.net/swirc/pdcurses-3.4.cab", "pdcurses-3.4.cab");
get_file("http://nifty-networks.net/swirc/swirc-royal.ico", "swirc-royal.ico");
