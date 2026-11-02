// Diagnostic decoding is bounded independently of authored-change validation.
static void test_native_payload_diagnostics(){
    constexpr dbpf::Key key{0x01897C18,0x01897C18,0x01897C18};
    constexpr size_t limit=4u<<20;
    auto put=[](std::vector<uint8_t>& bytes,size_t at,uint32_t value){
        for(unsigned n=0;n<4;++n)bytes[at+n]=uint8_t(value>>(n*8));
    };
    auto packed_package=[&](const std::vector<uint8_t>& packed,uint32_t size){
        auto bytes=dbpf::write({{key,packed}});
        const size_t index=96+packed.size()+8;
        put(bytes,index+20,size);bytes[index+24]=bytes[index+25]=0xFF;
        return bytes;
    };
    std::vector<uint8_t> body(1237480);
    for(size_t i=0;i<body.size();++i)body[i]=uint8_t(i*17);
    std::vector<uint8_t> output{0xA5};
    auto bytes=dbpf::write({{key,body}});
    bytes.insert(bytes.begin(),{0xF9,0xD4,0x23,0xB5,17,0,0,0});
    CHECK(diagnostic_payload(bytes,output)&&output==body);
    // Exercise the compressed form used by actual native scenario bodies.
    std::vector<uint8_t> packed{0x10,0xFB,uint8_t(body.size()>>16),uint8_t(body.size()>>8),uint8_t(body.size())};
    size_t at=0;
    while(body.size()-at>=4){
        const auto count=std::min<size_t>(112,(body.size()-at)&~size_t(3));
        packed.push_back(uint8_t(0xE0+((count-4)/4)));
        packed.insert(packed.end(),body.begin()+at,body.begin()+at+count);at+=count;
    }
    packed.push_back(uint8_t(0xFC+body.size()-at));packed.insert(packed.end(),body.begin()+at,body.end());
    CHECK(diagnostic_payload(packed_package(packed,uint32_t(body.size())),output)&&output==body);
    body.resize(limit,0x5A);CHECK(diagnostic_payload(dbpf::write({{key,body}}),output)&&output==body);
    body.push_back(0xA5);output={0x5A};
    CHECK(!diagnostic_payload(dbpf::write({{key,body}}),output)&&output==std::vector<uint8_t>{0x5A});
    // Both the indexed size and the RefPack size must pass the 4 MiB cap
    // before the decoder can reserve or overwrite the diagnostic output.
    const std::vector<uint8_t> oversized{0x90,0xFB,0,0x40,0,1,0xFC};
    CHECK(!diagnostic_payload(packed_package(oversized,1),output)&&output==std::vector<uint8_t>{0x5A});
    CHECK(!diagnostic_payload(packed_package({0x10,0xFB,0,0,1,0xFD,0x33},uint32_t(limit+1)),output)&&output==std::vector<uint8_t>{0x5A});
    CHECK(!diagnostic_payload(packed_package({0x90,0xFB,0,0,1},1),output)&&output==std::vector<uint8_t>{0x5A});
    CHECK(!diagnostic_payload(dbpf::write({{{1,2,3},{1,2,3}}}),output)&&output==std::vector<uint8_t>{0x5A});
    CHECK(!diagnostic_payload({1,2,3},output)&&output==std::vector<uint8_t>{0x5A});
    bytes=dbpf::write({{key,{1,2,3}}});
    bytes.insert(bytes.begin(),{'D','B','P','F','D','B','P','F','D','B','P','F','D','B','P','F'});
    CHECK(!diagnostic_payload(bytes,output)&&output==std::vector<uint8_t>{0x5A});
}
