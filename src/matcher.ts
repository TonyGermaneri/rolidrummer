import {reactive} from 'vue'
// Editing a search is a session draft; only Save writes it to the patch library.
export const matcher=reactive({autoMode:true,query:'',seed:'0',phraseBars:8,variationCount:0,source:''})
