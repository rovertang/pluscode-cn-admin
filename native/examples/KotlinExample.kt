import com.askcodex.pluscode.PlusCodeIndex

fun main(args: Array<String>) {
    PlusCodeIndex(args[0]).use { index ->
        val result = index.lookupLatLng(39.9042, 116.4074)
        check(result.isMatched)
        check(result.admin.countyCode == "110101")
        println("${result.admin.province} / ${result.admin.city} / ${result.admin.county}")
        println("length=${result.matchedLength}, boundary=${result.boundaryCell}")
        check(index.lookup("8P67C2C9+GP").isAmbiguous)
    }
}
